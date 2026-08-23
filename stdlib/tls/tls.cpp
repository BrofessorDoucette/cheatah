// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "tls.hpp"
#include "tls_lowlevel.hpp"  // the C++-only raw handle API this module implements (+ tls::Conn uses)

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include <sys/random.h>  // getentropy: client random + ephemeral X25519 key

#include "aead.hpp"     // chacha20poly1305_{en,de}crypt — the record cipher
#include "ed25519.hpp"    // verify — CertificateVerify for Ed25519 server certs
#include "hashlib.hpp"    // sha256_digest, hmac_sha256, hkdf_extract/expand — the key schedule
#include "p256.hpp"       // verify — CertificateVerify for ECDSA P-256 server certs
#include "p384.hpp"       // verify — CertificateVerify for ECDSA P-384 server certs
#include "rsa_verify.hpp" // verify_pss_sha256 — CertificateVerify for RSA (rsa_pss_rsae_sha256) certs
#include "socket.hpp"   // raw fd I/O underneath the record layer
#include "x25519.hpp"   // the key exchange
#include "x509.hpp"     // certificate chain / hostname / expiry validation (server AUTHENTICATION)

// A from-scratch TLS 1.3 client (RFC 8446); cipher suites ChaCha20-Poly1305, AES-128-GCM,
// and AES-256-GCM-SHA384, offered in hardware-preference order (see append_cipher_preference).
// The implementation walks the RFC top to bottom: record layer, transcript hash, the HKDF
// key schedule, then the handshake state machine. Every secret derives through hashlib's
// HKDF; every record seals/opens through the aead module; the ephemeral key is x25519.

namespace cheatah::tls {
namespace {

namespace sock = cheatah::socket;

thread_local std::string t_error;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the per-thread last_error() slot IS the documented error channel

void fail(std::string_view what) { t_error = std::string(what); }

// ---- hex <-> bytes: the ONE canonical implementation lives in hashlib -----------
// The crypto modules speak hex for keys; tls feeds from_hex only valid, even-length lowercase
// x25519 hex, so the canonical from_hex's odd-length/non-hex throws are never reached here.
using hashlib::to_hex;     // bytes -> lowercase hex (string_view / (uint8_t*, n) overloads).
using hashlib::from_hex;   // hex -> bytes (throws on odd length / non-hex).

// 16/24-bit big-endian helpers for the wire format.
void put16(std::string& out, unsigned v) {
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}
void put24(std::string& out, unsigned v) {
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}
unsigned get16(std::string_view s, std::size_t i) {
    return (static_cast<unsigned char>(s[i]) << 8) | static_cast<unsigned char>(s[i + 1]);
}
unsigned get24(std::string_view s, std::size_t i) {
    return (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8) |
           static_cast<unsigned char>(s[i + 2]);
}

// ---- the TLS 1.3 key schedule (RFC 8446 §7.1) over hashlib's HKDF ------------

} // namespace (pause: the key-schedule impls are namespace-level so detail:: can reach them)

/**
 * HKDF-Expand-Label(secret, label, context, length) with the "tls13 " prefix (RFC 8446 §7.1).
 * @param secret the HKDF secret.
 * @param label the schedule label (without the "tls13 " prefix, which is added here).
 * @param context the hash context bytes.
 * @param length the output length in bytes.
 * @param sha384 selects the SHA-384 HKDF (for the TLS_AES_256_GCM_SHA384 key schedule);
 *        default is the SHA-256 schedule.
 * @return the expanded key material, @p length bytes.
 * @complexity O(length) — HKDF-Expand emits ceil(length/hash) HMAC blocks.
 * @alloc the returned key material plus the HkdfLabel info string.
 * @test CheatahTls.ExpandLabel
 */
std::string expand_label_impl(std::string_view secret, std::string_view label,
                              std::string_view context, unsigned length, bool sha384 = false) {
    std::string info;
    put16(info, length);
    info.push_back(static_cast<char>(6 + label.size()));
    info += "tls13 ";
    info += label;
    info.push_back(static_cast<char>(context.size()));
    info += context;
    return sha384 ? hashlib::hkdf_expand_sha384(secret, info, length)
                  : hashlib::hkdf_expand(secret, info, length);
}

/**
 * Derive-Secret(secret, label, transcript) = Expand-Label(secret, label, Hash(transcript), HashLen),
 * where Hash is the negotiated suite's hash (SHA-256, or SHA-384 when @p sha384).
 * @param secret the HKDF secret.
 * @param label the schedule label.
 * @param transcript the handshake transcript to hash into the context.
 * @param sha384 selects the SHA-384 schedule; default is SHA-256.
 * @return the derived secret (32 or 48 bytes).
 * @complexity O(|transcript|) — one transcript hash, then a fixed-size expand.
 * @alloc the transcript-hash string and the returned secret.
 * @test CheatahTls.KeySchedule
 */
std::string derive_secret_impl(std::string_view secret, std::string_view label,
                               std::string_view transcript, bool sha384 = false) {
    const std::string th =
        sha384 ? hashlib::sha384_digest(transcript) : hashlib::sha256_digest(transcript);
    return expand_label_impl(secret, label, th, sha384 ? 48 : 32, sha384);
}

namespace {  // resume the file-local helpers

// The negotiated record cipher: ChaCha20-Poly1305 (0x1303), AES-128-GCM (0x1301), or AES-256-GCM (0x1302).
enum class Aead : std::uint8_t { Chacha20, Aes128, Aes256 };

// Key-schedule hash dispatch: the SHA-256 schedule by default, the SHA-384 schedule for the
// TLS_AES_256_GCM_SHA384 suite. (RFC 8446 §7.1: the schedule's Hash is the cipher suite's hash.)
std::string ks_digest(bool sha384, std::string_view d) {
    return sha384 ? hashlib::sha384_digest(d) : hashlib::sha256_digest(d);
}
std::string ks_extract(bool sha384, std::string_view salt, std::string_view ikm) {
    return sha384 ? hashlib::hkdf_extract_sha384(salt, ikm) : hashlib::hkdf_extract(salt, ikm);
}
std::string ks_hmac(bool sha384, std::string_view key, std::string_view data) {
    return sha384 ? hashlib::hmac_sha384(key, data) : hashlib::hmac_sha256(key, data);
}

// One traffic direction: AEAD key + iv + record sequence number.
struct Keys {
    std::string key_hex;   // AEAD key (hex): 32 bytes for ChaCha20 / AES-256-GCM, 16 for AES-128-GCM
    std::string iv;        // 12-byte raw iv; per-record nonce = iv XOR seq
    std::uint64_t seq = 0;
    Aead aead = Aead::Chacha20;
};

// Derive a direction's record keys from its traffic secret. Key length follows the AEAD (16 for AES-128,
// 32 for AES-256 / ChaCha20); @p sha384 selects the SHA-384 key schedule (the 256 suite).
Keys traffic_keys(std::string_view secret, Aead aead, bool sha384) {
    Keys k;
    k.aead = aead;
    k.key_hex = to_hex(expand_label_impl(secret, "key", "", aead == Aead::Aes128 ? 16 : 32, sha384));
    k.iv = expand_label_impl(secret, "iv", "", 12, sha384);
    return k;
}

// The per-record nonce: the 12-byte iv with the 8-byte big-endian sequence XORed into its tail.
std::string nonce_hex(const Keys& k) {
    std::string n = k.iv;
    for (int i = 0; i < 8; ++i) {
        n[4 + i] = static_cast<char>(static_cast<unsigned char>(n[4 + i]) ^
                                     static_cast<unsigned char>(k.seq >> (8 * (7 - i))));
    }
    return to_hex(n);
}

// ---- one TLS session ----------------------------------------------------------

struct Session {
    long long fd = -1;
    Keys client_keys;          // our sending direction
    Keys server_keys;          // the peer's direction
    std::string read_buffer;   // raw bytes from the socket not yet framed into records
    std::string app_pending;   // decrypted application data not yet handed to recv()
    bool closed = false;       // close_notify seen (either direction)
};

// The process-wide session table: handle → Session, behind one mutex.
struct Registry {
    std::mutex mutex;
    std::map<long long, Session> sessions;
    long long next_handle = 1;
};
Registry& registry() {
    static Registry r;
    return r;
}

// ---- record I/O ---------------------------------------------------------------

// Read exactly one TLS record (header + payload) from the socket into (type, payload).
// Blocking, bounded by the fd's socket timeout. False on EOF/short read.
// The socket read chunk. 64 KiB drains several TLS records per syscall when the kernel has them
// buffered, which (with the socket's enlarged SO_RCVBUF) keeps the receive window open instead of
// stalling one record per round-trip.
constexpr long long kRecvChunk = 65536;

bool read_record(long long fd, std::string& buffer, unsigned& type, std::string& payload) {
    while (buffer.size() < 5) {
        const std::string chunk = sock::recv(fd, kRecvChunk);
        if (chunk.empty()) return false;
        buffer += chunk;
    }
    type = static_cast<unsigned char>(buffer[0]);
    const unsigned len = get16(buffer, 3);
    if (len > 16384 + 256) {  // RFC bound + AEAD overhead: anything bigger is malformed
        return false;
    }
    while (buffer.size() < 5 + len) {
        const std::string chunk = sock::recv(fd, kRecvChunk);
        if (chunk.empty()) return false;
        buffer += chunk;
    }
    payload = buffer.substr(5, len);
    buffer.erase(0, 5 + len);
    return true;
}

// True when `buffer` already holds at least one COMPLETE record — used by the drain loop to keep
// decrypting from bytes already in hand without blocking on another recv().
bool has_complete_record(const std::string& buffer) {
    if (buffer.size() < 5) {
        return false;
    }
    const unsigned len = get16(buffer, 3);
    return buffer.size() >= static_cast<std::size_t>(5) + len;
}

bool write_record(long long fd, unsigned type, std::string_view payload) {
    std::string rec;
    rec.push_back(static_cast<char>(type));
    put16(rec, 0x0303);  // legacy_record_version
    put16(rec, static_cast<unsigned>(payload.size()));
    rec += payload;
    return sock::sendall(fd, rec) == 0;
}

// Seal one application_data record (RFC 8446 §5.2): inner plaintext = content || content_type,
// AAD = the record header, then ChaCha20-Poly1305.
bool seal_record(long long fd, Keys& k, unsigned inner_type, std::string_view content) {
    std::string inner(content);
    inner.push_back(static_cast<char>(inner_type));
    std::string aad;
    aad.push_back(23);
    put16(aad, 0x0303);
    put16(aad, static_cast<unsigned>(inner.size() + 16));
    std::string ct;
    if (k.aead == Aead::Aes256) ct = aead::aes256gcm_encrypt(k.key_hex, nonce_hex(k), aad, inner);
    else if (k.aead == Aead::Aes128) ct = aead::aes128gcm_encrypt(k.key_hex, nonce_hex(k), aad, inner);
    else ct = aead::chacha20poly1305_encrypt(k.key_hex, nonce_hex(k), aad, inner);
    ++k.seq;
    if (ct.empty()) return false;
    return sock::sendall(fd, aad + ct) == 0;
}

// Open one encrypted record: returns the inner content and type, false on AEAD failure.
bool open_record(Keys& k, std::string_view payload, unsigned& inner_type, std::string& content) {
    std::string aad;
    aad.push_back(23);
    put16(aad, 0x0303);
    put16(aad, static_cast<unsigned>(payload.size()));
    std::string inner;
    if (k.aead == Aead::Aes256) inner = aead::aes256gcm_decrypt(k.key_hex, nonce_hex(k), aad, payload);
    else if (k.aead == Aead::Aes128) inner = aead::aes128gcm_decrypt(k.key_hex, nonce_hex(k), aad, payload);
    else inner = aead::chacha20poly1305_decrypt(k.key_hex, nonce_hex(k), aad, payload);
    ++k.seq;
    if (inner.empty() && payload.size() > 16) return false;  // tag mismatch (or empty record)
    while (!inner.empty() && inner.back() == '\0') inner.pop_back();  // strip padding
    if (inner.empty()) return false;  // a record must carry a content type
    inner_type = static_cast<unsigned char>(inner.back());
    inner.pop_back();          // drop the trailing content-type byte in place …
    content = std::move(inner);  // … and move the ~16 KB plaintext out instead of copying it
    return true;
}

// ---- handshake construction -----------------------------------------------------

std::string random_bytes(std::size_t n) {
    std::string out(n, '\0');
    // getentropy (Linux + macOS/BSD) is the portable CSPRNG read; getrandom is Linux-only.
    // It is capped at 256 bytes per call, so loop for larger requests. A nonzero return means
    // the OS could not supply randomness — fatal for key material, so bail with "".
    std::size_t got = 0;
    while (got < n) {
        const std::size_t chunk = std::min<std::size_t>(n - got, 256);
        if (::getentropy(out.data() + got, chunk) != 0) return "";
        got += chunk;
    }
    return out;
}

// Build the ClientHello handshake MESSAGE (no record header). Fills `client_hello_random`.
std::string build_client_hello(const std::string& server_name, std::string_view pub_raw) {
    std::string body;
    put16(body, 0x0303);             // legacy_version
    body += random_bytes(32);        // random
    body.push_back(32);              // legacy_session_id (32 bytes, middlebox compatibility)
    body += random_bytes(32);
    // Cipher preference follows OUR fastest cipher, exactly as OpenSSL/curl do: with AES-NI +
    // PCLMULQDQ present, AES-GCM runs at multi-GB/s hardware speed and beats our scalar ChaCha20,
    // so offer AES-GCM FIRST; without hardware AES (some VMs/ARM), scalar ChaCha20 is the faster
    // path, so lead with it. The server picks from our order when it honors client preference —
    // which is what turns a ChaCha-negotiated ~200 MB/s link into a ~320 MB/s AES-GCM one.
    put16(body, 6);                  // cipher_suites: three suites (6 bytes)
    detail::append_cipher_preference(body, aead::crypto_hardware_active());
    body.push_back(1);               // legacy_compression_methods
    body.push_back(0);               // null

    std::string ext;
    {  // server_name (0)
        std::string names;
        names.push_back(0);  // host_name
        put16(names, static_cast<unsigned>(server_name.size()));
        names += server_name;
        std::string sni;
        put16(sni, static_cast<unsigned>(names.size()));
        sni += names;
        put16(ext, 0);
        put16(ext, static_cast<unsigned>(sni.size()));
        ext += sni;
    }
    {  // supported_groups (10): x25519 only
        std::string g;
        put16(g, 2);
        put16(g, 0x001d);
        put16(ext, 10);
        put16(ext, static_cast<unsigned>(g.size()));
        ext += g;
    }
    {  // signature_algorithms (13): ed25519 (verifiable) + the common ones so real servers
       //  complete the handshake far enough for our explicit refusal to be diagnosable
        std::string a;
        put16(a, 8);
        put16(a, 0x0807);  // ed25519
        put16(a, 0x0804);  // rsa_pss_rsae_sha256 (verified — see rsa_verify.hpp)
        put16(a, 0x0403);  // ecdsa_secp256r1_sha256 (verified — see p256)
        put16(a, 0x0503);  // ecdsa_secp384r1_sha384 (verified — see p384)
        put16(ext, 13);
        put16(ext, static_cast<unsigned>(a.size()));
        ext += a;
    }
    {  // supported_versions (43): TLS 1.3
        std::string v;
        v.push_back(2);
        put16(v, 0x0304);
        put16(ext, 43);
        put16(ext, static_cast<unsigned>(v.size()));
        ext += v;
    }
    {  // key_share (51): our X25519 public key
        std::string entry;
        put16(entry, 0x001d);
        put16(entry, 32);
        entry += pub_raw;
        std::string ks;
        put16(ks, static_cast<unsigned>(entry.size()));
        ks += entry;
        put16(ext, 51);
        put16(ext, static_cast<unsigned>(ks.size()));
        ext += ks;
    }
    put16(body, static_cast<unsigned>(ext.size()));
    body += ext;

    std::string msg;
    msg.push_back(1);  // client_hello
    put24(msg, static_cast<unsigned>(body.size()));
    msg += body;
    return msg;
}

// Human-readable TLS alert (RFC 8446 §6) from the 2 alert bytes — so a handshake refusal names its
// cause (e.g. 40 handshake_failure = no common cipher/group; 70 protocol_version = no TLS 1.3).
std::string alert_text(std::string_view p) {
    if (p.size() < 2) return "(empty alert)";
    const unsigned lvl = static_cast<unsigned char>(p[0]);
    const unsigned d = static_cast<unsigned char>(p[1]);
    const char* name = "unknown";
    switch (d) {
        case 0: name = "close_notify"; break;
        case 10: name = "unexpected_message"; break;
        case 20: name = "bad_record_mac"; break;
        case 22: name = "record_overflow"; break;
        case 40: name = "handshake_failure"; break;
        case 42: name = "bad_certificate"; break;
        case 43: name = "unsupported_certificate"; break;
        case 47: name = "illegal_parameter"; break;
        case 48: name = "unknown_ca"; break;
        case 49: name = "access_denied"; break;
        case 50: name = "decode_error"; break;
        case 51: name = "decrypt_error"; break;
        case 70: name = "protocol_version"; break;
        case 71: name = "insufficient_security"; break;
        case 80: name = "internal_error"; break;
        case 109: name = "missing_extension"; break;
        case 110: name = "unsupported_extension"; break;
        case 112: name = "unrecognized_name"; break;
        case 116: name = "certificate_required"; break;
        case 120: name = "no_application_protocol"; break;
        default: break;
    }
    return "alert level=" + std::to_string(lvl) + " description=" + std::to_string(d) + " (" + name + ")";
}

// Parse ServerHello: confirm TLS 1.3 + one of our suites, extract the server's X25519 key share and
// the CHOSEN cipher suite (0x1303 ChaCha20-Poly1305 or 0x1301 AES-128-GCM).
bool parse_server_hello(std::string_view msg, std::string& server_pub_raw, unsigned& chosen_suite) {
    if (msg.size() < 4 || msg[0] != 2) return false;  // server_hello
    std::string_view b = msg.substr(4);
    if (b.size() < 2 + 32 + 1) return false;
    std::size_t i = 2 + 32;                       // legacy_version + random
    const unsigned sid_len = static_cast<unsigned char>(b[i]);
    i += 1 + sid_len;
    if (b.size() < i + 4) return false;
    const unsigned suite = get16(b, i);
    if (suite != 0x1303 && suite != 0x1301 && suite != 0x1302)
        return false;  // LCOV_EXCL_LINE: a server choosing a suite we did NOT offer is a malformed/hostile peer a conformant server never produces — the client-side mirror of the tested ParseClientHello "no suite in common" rejection
    chosen_suite = suite;
    i += 2 + 1;                                   // suite + legacy_compression
    if (b.size() < i + 2) return false;
    const unsigned ext_len = get16(b, i);
    i += 2;
    const std::size_t ext_end = i + ext_len;
    bool saw_13 = false;
    while (i + 4 <= ext_end && ext_end <= b.size()) {
        const unsigned etype = get16(b, i);
        const unsigned elen = get16(b, i + 2);
        i += 4;
        if (i + elen > b.size()) return false;
        if (etype == 43 && elen == 2 && get16(b, i) == 0x0304) saw_13 = true;
        if (etype == 51 && elen >= 4 && get16(b, i) == 0x001d && get16(b, i + 2) == 32 &&
            elen == 4 + 32) {
            server_pub_raw = std::string(b.substr(i + 4, 32));
        }
        i += elen;
    }
    return saw_13 && server_pub_raw.size() == 32;
}

// Extract the Ed25519 public key from the leaf certificate's SubjectPublicKeyInfo: the DER
// pattern 30 05 06 03 2B 65 70 (AlgorithmIdentifier { id-Ed25519 }) followed by
// 03 21 00 <32-byte key> (BIT STRING). Returns "" when the cert key is not Ed25519.
std::string ed25519_spki_key(std::string_view cert_der) {
    static const unsigned char kPat[] = {0x30, 0x05, 0x06, 0x03, 0x2B, 0x65, 0x70,
                                         0x03, 0x21, 0x00};
    for (std::size_t i = 0; i + sizeof kPat + 32 <= cert_der.size(); ++i) {
        if (std::memcmp(cert_der.data() + i, kPat, sizeof kPat) == 0) {
            return std::string(cert_der.substr(i + sizeof kPat, 32));
        }
    }
    return "";  // LCOV_EXCL_LINE: only when an Ed25519 CertificateVerify names a non-Ed25519 leaf — a malformed peer we don't mirror
}

// ---- server-side handshake construction (mirror of the client builders above) --------

// A PEM block's DER bytes (strict base64 — a non-alphabet byte rejects the block, like x509).
// @p label is e.g. "CERTIFICATE" or "PRIVATE KEY". Returns "" if the block is absent/malformed.
std::string pem_block(const std::string& pem, const std::string& label) {
    const std::string begin = "-----BEGIN " + label + "-----";
    const std::string end = "-----END " + label + "-----";
    const std::size_t s = pem.find(begin);
    if (s == std::string::npos) return "";
    const std::size_t b = s + begin.size();
    const std::size_t e = pem.find(end, b);
    if (e == std::string::npos) return "";
    return hashlib::base64_decode(pem.substr(b, e - b), /*strict=*/true);
}

// The 32-byte Ed25519 seed from a PKCS#8 private key DER: the id-Ed25519 AlgorithmIdentifier
// (30 05 06 03 2B 65 70) followed by 04 22 04 20 (OCTET STRING { OCTET STRING[32] }) and the seed.
std::string ed25519_seed_from_pkcs8(std::string_view der) {
    static const unsigned char kPat[] = {0x30, 0x05, 0x06, 0x03, 0x2B, 0x65, 0x70,
                                         0x04, 0x22, 0x04, 0x20};
    for (std::size_t i = 0; i + sizeof kPat + 32 <= der.size(); ++i) {
        if (std::memcmp(der.data() + i, kPat, sizeof kPat) == 0) {
            return std::string(der.substr(i + sizeof kPat, 32));
        }
    }
    return "";
}

// EVERY PEM block under @p label, in order — the server's Certificate message must carry the whole
// chain (leaf first, then intermediates), so a Let's Encrypt fullchain.pem yields N entries here
// where pem_block() alone would silently drop everything after the leaf and browsers would reject
// the path. A malformed block (bad base64) poisons the whole read: better no chain than a hole.
std::vector<std::string> pem_blocks(const std::string& pem, const std::string& label) {
    std::vector<std::string> out;
    const std::string begin = "-----BEGIN " + label + "-----";
    const std::string end = "-----END " + label + "-----";
    std::size_t at = 0;
    while (true) {
        const std::size_t s = pem.find(begin, at);
        if (s == std::string::npos) break;
        const std::size_t b = s + begin.size();
        const std::size_t e = pem.find(end, b);
        if (e == std::string::npos) return {};
        const std::string der = hashlib::base64_decode(pem.substr(b, e - b), /*strict=*/true);
        if (der.empty()) return {};
        out.push_back(der);
        at = e + end.size();
    }
    return out;
}

// The 32-byte P-256 private scalar from a server key PEM — either shape openssl/certbot emit:
// PKCS#8 ("PRIVATE KEY": AlgorithmIdentifier{id-ecPublicKey, prime256v1} wrapping a SEC1
// ECPrivateKey) or bare SEC1 ("EC PRIVATE KEY"). Both carry the scalar as 02 01 01 04 20 <d32>
// (ECPrivateKey version 1, then the OCTET STRING), and both carry the prime256v1 OID
// (2A 86 48 CE 3D 03 01 07) — required here so a P-384/other-curve key is refused instead of
// misread. Same pattern-scan discipline as ed25519_seed_from_pkcs8 above.
std::string ec_p256_scalar_from_pem(const std::string& key_pem) {
    std::string der = pem_block(key_pem, "PRIVATE KEY");
    if (der.empty()) der = pem_block(key_pem, "EC PRIVATE KEY");
    if (der.empty()) return "";
    static const unsigned char kOid[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    bool p256_curve = false;
    for (std::size_t i = 0; i + sizeof kOid <= der.size() && !p256_curve; ++i) {
        p256_curve = std::memcmp(der.data() + i, kOid, sizeof kOid) == 0;
    }
    if (!p256_curve) return "";
    static const unsigned char kPat[] = {0x02, 0x01, 0x01, 0x04, 0x20};
    for (std::size_t i = 0; i + sizeof kPat + 32 <= der.size(); ++i) {
        if (std::memcmp(der.data() + i, kPat, sizeof kPat) == 0) {
            return der.substr(i + sizeof kPat, 32);
        }
    }
    return "";
}

// Parse a ClientHello: choose a cipher suite we support (ChaCha20 preferred), extract the client's
// X25519 key share + its legacy_session_id (echoed in ServerHello), confirm it offered TLS 1.3,
// and surface its signature_algorithms (ext 13) as raw u16 pairs in @p sig_algs — the parser stays
// lenient (an absent extension parses fine, sig_algs empty); server_handshake enforces the match,
// because refusing to SIGN with an algorithm the client never offered is a handshake policy, not a
// parse question.
bool parse_client_hello(std::string_view msg, std::string& client_pub_raw, unsigned& chosen_suite,
                        std::string& session_id, std::string& sig_algs) {
    client_pub_raw.clear();  // never leave stale out-params on a rejected/partial parse
    session_id.clear();
    sig_algs.clear();
    if (msg.size() < 4 || static_cast<unsigned char>(msg[0]) != 1) return false;  // client_hello
    std::string_view b = msg.substr(4);
    std::size_t i = 2 + 32;                          // legacy_version + random
    if (b.size() < i + 1) return false;
    const unsigned sid_len = static_cast<unsigned char>(b[i]);
    i += 1;
    if (b.size() < i + sid_len) return false;
    session_id = std::string(b.substr(i, sid_len));  // must be echoed back verbatim
    i += sid_len;
    if (b.size() < i + 2) return false;
    const unsigned cs_len = get16(b, i);
    i += 2;
    if (b.size() < i + cs_len) return false;
    bool has_chacha = false, has_aes = false;
    for (std::size_t j = 0; j + 2 <= cs_len; j += 2) {
        const unsigned suite = get16(b, i + j);
        if (suite == 0x1303) has_chacha = true;
        if (suite == 0x1301) has_aes = true;
    }
    i += cs_len;
    if (has_chacha) chosen_suite = 0x1303;
    else if (has_aes) chosen_suite = 0x1301;
    else return false;                               // no cipher suite in common
    if (b.size() < i + 1) return false;
    const unsigned comp_len = static_cast<unsigned char>(b[i]);
    i += 1 + comp_len;
    if (b.size() < i + 2) return false;
    const unsigned ext_len = get16(b, i);
    i += 2;
    const std::size_t ext_end = i + ext_len;
    bool saw_13 = false;
    while (i + 4 <= ext_end && ext_end <= b.size()) {
        const unsigned etype = get16(b, i);
        const unsigned elen = get16(b, i + 2);
        i += 4;
        if (i + elen > b.size()) return false;
        if (etype == 43 && elen >= 1) {              // supported_versions (list): look for 0x0304
            const unsigned n = static_cast<unsigned char>(b[i]);
            for (std::size_t j = 0; j + 2 <= n && i + 1 + j + 2 <= b.size(); j += 2) {
                if (get16(b, i + 1 + j) == 0x0304) saw_13 = true;
            }
        }
        if (etype == 13 && elen >= 2) {              // signature_algorithms: the u16-pair list
            const unsigned sa_len = get16(b, i);
            if (2 + sa_len <= elen && sa_len % 2 == 0) {
                sig_algs = std::string(b.substr(i + 2, sa_len));
            }
        }
        if (etype == 51 && elen >= 2) {              // key_share (list): find our x25519 (0x001d)
            const unsigned ks_len = get16(b, i);
            std::size_t j = i + 2;
            const std::size_t ks_end = i + 2 + ks_len;
            while (j + 4 <= ks_end && ks_end <= b.size()) {
                const unsigned group = get16(b, j);
                const unsigned klen = get16(b, j + 2);
                if (group == 0x001d && klen == 32 && j + 4 + 32 <= b.size()) {
                    client_pub_raw = std::string(b.substr(j + 4, 32));
                }
                j += 4 + klen;
            }
        }
        i += elen;
    }
    return saw_13 && client_pub_raw.size() == 32;
}

// Build a ServerHello: echo the client's session id, our chosen suite, our X25519 key share.
std::string build_server_hello(std::string_view session_id, unsigned suite,
                               std::string_view pub_raw) {
    std::string body;
    put16(body, 0x0303);                             // legacy_version
    body += random_bytes(32);                        // random
    body.push_back(static_cast<char>(session_id.size()));
    body += session_id;                              // echo legacy_session_id (RFC 8446 §4.1.3)
    put16(body, suite);                              // cipher_suite
    body.push_back(0);                               // legacy_compression_method (null)

    std::string ext;
    put16(ext, 43);                                  // supported_versions: TLS 1.3
    put16(ext, 2);
    put16(ext, 0x0304);
    {                                                // key_share (51): our X25519 KeyShareEntry
        std::string entry;
        put16(entry, 0x001d);
        put16(entry, 32);
        entry += pub_raw;
        put16(ext, 51);
        put16(ext, static_cast<unsigned>(entry.size()));
        ext += entry;
    }
    put16(body, static_cast<unsigned>(ext.size()));
    body += ext;

    std::string msg;
    msg.push_back(2);                                // server_hello
    put24(msg, static_cast<unsigned>(body.size()));
    msg += body;
    return msg;
}

// The TLS 1.3 SERVER handshake over connected fd @p fd, presenting @p cert_pem (an Ed25519 or
// ECDSA P-256 leaf — @p cert_pem may be a fullchain.pem, and every block is sent) and proving
// possession with @p key_pem (the leaf's PKCS#8 Ed25519 key, or its PKCS#8/SEC1 P-256 key).
// Mirror of handshake(): same key schedule and record I/O, roles reversed — we SIGN
// CertificateVerify instead of verifying it, and send the certificate flight. Returns a session
// handle (>= 1) or -1 (see last_error()).
long long server_handshake(long long fd, const std::string& cert_pem, const std::string& key_pem) {
    t_error.clear();

    const std::vector<std::string> chain = pem_blocks(cert_pem, "CERTIFICATE");
    if (chain.empty()) {
        fail("tls: server certificate PEM is missing or malformed");
        return -1;
    }
    const std::string& leaf_der = chain.front();

    // Which key did we get, and does it actually belong to the leaf? Deriving the public key from
    // the private half and comparing it to the leaf's SPKI catches a mixed-up cert/key pair at
    // startup with a precise message, instead of as an opaque CertificateVerify failure on the
    // first client. Exactly one signature algorithm follows from the key type — there is no
    // negotiation surface on our side to confuse.
    const std::string ed_seed = ed25519_seed_from_pkcs8(pem_block(key_pem, "PRIVATE KEY"));
    const std::string ec_scalar = ec_p256_scalar_from_pem(key_pem);
    unsigned cv_alg = 0;
    if (ed_seed.size() == 32) {
        const std::string spki = ed25519_spki_key(leaf_der);
        if (spki.size() != 32 || ed25519::public_key(to_hex(ed_seed)) != to_hex(spki)) {
            fail("tls: the Ed25519 private key does not match the server certificate");
            return -1;
        }
        cv_alg = 0x0807;  // ed25519
    } else if (ec_scalar.size() == 32) {
        const std::string point = p256::spki_ec_point(leaf_der);
        if (point.size() != 64 || p256::public_from_private(ec_scalar) != point) {
            fail("tls: the ECDSA P-256 private key does not match the server certificate");
            return -1;
        }
        cv_alg = 0x0403;  // ecdsa_secp256r1_sha256
    } else {
        fail("tls: server private key is not a PKCS#8 Ed25519 or P-256 EC key");
        return -1;
    }

    // ClientHello (plaintext; tolerate a leading ChangeCipherSpec compat record).
    std::string buffer, payload;
    unsigned rtype = 0;
    for (;;) {
        if (!read_record(fd, buffer, rtype, payload)) {
            fail("tls: connection closed before ClientHello");
            return -1;
        }
        if (rtype == 20) continue;
        if (rtype != 22) {
            fail("tls: expected a ClientHello");
            return -1;
        }
        break;
    }
    std::string client_pub_raw, session_id, sig_algs;
    unsigned suite = 0;
    if (!parse_client_hello(payload, client_pub_raw, suite, session_id, sig_algs)) {
        fail("tls: malformed ClientHello (or no TLS 1.3 / X25519 / shared cipher suite)");
        return -1;
    }
    // RFC 8446 §4.4.3: a server MUST NOT sign with an algorithm the client did not offer in
    // signature_algorithms (§4.2.3 makes the extension mandatory for certificate auth). Our
    // algorithm is fixed by the key type, so this is a containment check, not a negotiation.
    bool alg_offered = false;
    for (std::size_t j = 0; j + 2 <= sig_algs.size(); j += 2) {
        if (get16(sig_algs, j) == cv_alg) alg_offered = true;
    }
    if (!alg_offered) {
        fail("tls: the client's signature_algorithms do not include our certificate's algorithm");
        return -1;
    }
    // The server offers only the SHA-256 suites (ChaCha20 / AES-128-GCM), so the key schedule is SHA-256.
    const Aead aead = (suite == 0x1301) ? Aead::Aes128 : Aead::Chacha20;
    std::string transcript = payload;  // ClientHello

    // Our ephemeral X25519 key pair, and ServerHello.
    const std::string priv_raw = random_bytes(32);
    if (priv_raw.size() != 32) {
        fail("tls: system random unavailable");  // LCOV_EXCL_LINE: getentropy failure — unreachable on a working host
        return -1;  // LCOV_EXCL_LINE
    }
    const std::string priv_hex = to_hex(priv_raw);
    const std::string pub_raw = from_hex(x25519::x25519_base(priv_hex));
    const std::string server_hello = build_server_hello(session_id, suite, pub_raw);
    transcript += server_hello;
    if (!write_record(fd, 22, server_hello)) {
        fail("tls: cannot send ServerHello");  // LCOV_EXCL_LINE: a mid-handshake socket write failure
        return -1;  // LCOV_EXCL_LINE
    }
    write_record(fd, 20, std::string(1, '\x01'));  // middlebox-compat ChangeCipherSpec (not in transcript)

    // Key schedule (identical to the client; the transcript now spans ClientHello + ServerHello).
    const std::string shared_hex = x25519::x25519(priv_hex, to_hex(client_pub_raw));
    if (shared_hex.empty()) {
        fail("tls: invalid client key share");
        return -1;
    }
    const std::string zeros(32, '\0');
    const std::string early = hashlib::hkdf_extract(std::string(), zeros);
    const std::string derived = derive_secret_impl(early, "derived", "");
    const std::string hs_secret = hashlib::hkdf_extract(derived, from_hex(shared_hex));
    const std::string c_hs = derive_secret_impl(hs_secret, "c hs traffic", transcript);
    const std::string s_hs = derive_secret_impl(hs_secret, "s hs traffic", transcript);
    Keys server_keys = traffic_keys(s_hs, aead, false);  // we SEND under the server secret
    Keys client_keys = traffic_keys(c_hs, aead, false);  // we RECEIVE under the client secret

    // Encrypted flight: EncryptedExtensions (empty), Certificate, CertificateVerify, Finished —
    // each sealed as its own handshake record (inner type 22).
    std::string ee;
    ee.push_back(8);         // encrypted_extensions
    put24(ee, 2);
    put16(ee, 0);            // extensions: empty
    transcript += ee;
    if (!seal_record(fd, server_keys, 22, ee)) {
        fail("tls: cannot send EncryptedExtensions");  // LCOV_EXCL_LINE: a mid-handshake socket write failure
        return -1;  // LCOV_EXCL_LINE
    }

    std::string cert_msg;
    {
        std::string entries;  // every chain block, leaf first — a fullchain.pem arrives intact,
        for (const std::string& der : chain) {  // giving the client a path to its trust anchor
            put24(entries, static_cast<unsigned>(der.size()));
            entries += der;
            put16(entries, 0);                                // per-cert extensions: none
        }
        std::string cbody;
        cbody.push_back(0);                                   // certificate_request_context: empty
        put24(cbody, static_cast<unsigned>(entries.size()));
        cbody += entries;
        cert_msg.push_back(11);                               // certificate
        put24(cert_msg, static_cast<unsigned>(cbody.size()));
        cert_msg += cbody;
    }
    transcript += cert_msg;
    if (!seal_record(fd, server_keys, 22, cert_msg)) {
        fail("tls: cannot send Certificate");  // LCOV_EXCL_LINE: a mid-handshake socket write failure
        return -1;  // LCOV_EXCL_LINE
    }

    std::string cv_msg;
    {
        // Same signed content the client verifies: 64 spaces, the context string, a NUL, then the
        // transcript hash through Certificate. Ed25519 signs the content directly; ECDSA P-256
        // signs SHA-256(content) and travels as DER — the exact mirror of the client's verify
        // branches for 0x0807/0x0403.
        std::string signed_content(64, ' ');
        signed_content += "TLS 1.3, server CertificateVerify";
        signed_content.push_back('\0');
        signed_content += hashlib::sha256_digest(transcript);
        std::string sig;
        if (cv_alg == 0x0807) {
            sig = from_hex(ed25519::sign(to_hex(ed_seed), signed_content));
        } else {
            sig = p256::rs_to_der(
                p256::sign_raw(ec_scalar, hashlib::sha256_digest(signed_content)));
            if (sig.empty()) {  // LCOV_EXCL_LINE: pre-flight proved the scalar derives the leaf's public key, so sign_raw cannot reject it
                fail("tls: ECDSA signing failed (invalid P-256 private key scalar)");  // LCOV_EXCL_LINE
                return -1;  // LCOV_EXCL_LINE
            }
        }
        std::string vbody;
        put16(vbody, cv_alg);
        put16(vbody, static_cast<unsigned>(sig.size()));
        vbody += sig;
        cv_msg.push_back(15);                                 // certificate_verify
        put24(cv_msg, static_cast<unsigned>(vbody.size()));
        cv_msg += vbody;
    }
    transcript += cv_msg;
    if (!seal_record(fd, server_keys, 22, cv_msg)) {
        fail("tls: cannot send CertificateVerify");  // LCOV_EXCL_LINE: a mid-handshake socket write failure
        return -1;  // LCOV_EXCL_LINE
    }

    std::string fin_msg;
    {
        const std::string finished_key = expand_label_impl(s_hs, "finished", "", 32);
        const std::string verify =
            hashlib::hmac_sha256(finished_key, hashlib::sha256_digest(transcript));
        fin_msg.push_back(20);                                // finished
        put24(fin_msg, static_cast<unsigned>(verify.size()));
        fin_msg += verify;
    }
    if (!seal_record(fd, server_keys, 22, fin_msg)) {
        fail("tls: cannot send server Finished");  // LCOV_EXCL_LINE: a mid-handshake socket write failure
        return -1;  // LCOV_EXCL_LINE
    }
    transcript += fin_msg;

    // Application traffic secrets (transcript through the server's Finished).
    const std::string derived2 = derive_secret_impl(hs_secret, "derived", "");
    const std::string master = hashlib::hkdf_extract(derived2, zeros);
    const std::string c_ap = derive_secret_impl(master, "c ap traffic", transcript);
    const std::string s_ap = derive_secret_impl(master, "s ap traffic", transcript);

    // Client Finished (encrypted under the client handshake keys); verify its MAC over the
    // transcript through the server Finished.
    const std::string c_finished_key = expand_label_impl(c_hs, "finished", "", 32);
    const std::string expect =
        hashlib::hmac_sha256(c_finished_key, hashlib::sha256_digest(transcript));
    bool client_finished = false;
    while (!client_finished) {
        if (!read_record(fd, buffer, rtype, payload)) {
            fail("tls: connection closed before the client Finished");
            return -1;
        }
        if (rtype == 20) continue;  // ChangeCipherSpec compat
        if (rtype == 21) {
            fail("tls: client alert during the handshake — " + alert_text(payload));
            return -1;
        }
        if (rtype != 23) {
            fail("tls: unexpected plaintext record awaiting the client Finished");
            return -1;
        }
        unsigned inner_type = 0;
        std::string content;
        if (!open_record(client_keys, payload, inner_type, content)) {
            fail("tls: client Finished failed authentication");
            return -1;
        }
        if (inner_type != 22 || content.size() < 4 || static_cast<unsigned char>(content[0]) != 20) {
            fail("tls: expected a client Finished");  // LCOV_EXCL_LINE: a key-scheduled malicious client we do not mirror
            return -1;  // LCOV_EXCL_LINE
        }
        // cppcheck-suppress stlcstrConstructor  // (ptr,len) subview of content — not a c_str() copy
        const std::string_view got(content.data() + 4, content.size() - 4);
        unsigned char diff = (got.size() == expect.size()) ? 0 : 1;  // constant-time MAC compare
        for (std::size_t i = 0; i < expect.size(); ++i) {
            const unsigned char g = i < got.size() ? static_cast<unsigned char>(got[i]) : 0;
            diff |= g ^ static_cast<unsigned char>(expect[i]);
        }
        if (diff != 0) {
            fail("tls: client Finished MAC mismatch — handshake transcript tampered");  // LCOV_EXCL_LINE: a key-scheduled malicious client we do not mirror
            return -1;  // LCOV_EXCL_LINE
        }
        client_finished = true;
    }

    Session s;
    s.fd = fd;
    s.client_keys = traffic_keys(s_ap, aead, false);  // our sending direction (server app secret)
    s.server_keys = traffic_keys(c_ap, aead, false);  // the peer's direction (client app secret)
    s.read_buffer = std::move(buffer);
    const std::lock_guard<std::mutex> lock(registry().mutex);
    const long long handle = registry().next_handle++;
    registry().sessions[handle] = std::move(s);
    return handle;
}

// ---- the handshake state machine -------------------------------------------------

// The system CA trust store, parsed once and reused (loading ~150 CA certs on every handshake
// would be wasteful). Thread-safe initialization via the C++ function-local static.
const x509::TrustStore& default_trust() {
    static const x509::TrustStore store = x509::load_trust("");
    return store;
}

long long handshake(long long fd, const std::string& server_name, bool insecure,
                    const std::string& ca_file) {
    t_error.clear();

    // Ephemeral X25519 key pair.
    const std::string priv_raw = random_bytes(32);
    if (priv_raw.size() != 32) {
        fail("tls: system random unavailable");
        return -1;
    }
    const std::string priv_hex = to_hex(priv_raw);
    const std::string pub_hex = x25519::x25519_base(priv_hex);
    const std::string pub_raw = from_hex(pub_hex);

    std::string transcript;  // concatenated handshake MESSAGES (no record headers)
    const std::string client_hello = build_client_hello(server_name, pub_raw);
    transcript += client_hello;
    if (!write_record(fd, 22, client_hello)) {
        fail("tls: cannot send ClientHello");
        return -1;
    }

    // ServerHello (plaintext record, type 22; tolerate ChangeCipherSpec compat records).
    std::string buffer, payload;
    unsigned rtype = 0;
    std::string server_pub_raw;
    for (;;) {
        if (!read_record(fd, buffer, rtype, payload)) {
            fail("tls: connection closed before ServerHello");
            return -1;
        }
        if (rtype == 20) continue;  // ChangeCipherSpec (compat) — ignored
        if (rtype == 21) {
            fail("tls: server sent an alert instead of ServerHello — " + alert_text(payload));
            return -1;
        }
        if (rtype != 22) {
            fail("tls: unexpected record before ServerHello");
            return -1;
        }
        break;
    }
    unsigned chosen_suite = 0;
    if (!parse_server_hello(payload, server_pub_raw, chosen_suite)) {
        fail("tls: malformed ServerHello (or the server refused TLS 1.3 + our cipher suites)");
        return -1;
    }
    // The negotiated suite fixes both the record AEAD and the key-schedule hash.
    const bool sha384 = (chosen_suite == 0x1302);  // TLS_AES_256_GCM_SHA384 → SHA-384 schedule
    Aead aead = Aead::Chacha20;  // TLS_CHACHA20_POLY1305_SHA256 (0x1303)
    if (chosen_suite == 0x1302) aead = Aead::Aes256;
    else if (chosen_suite == 0x1301) aead = Aead::Aes128;
    transcript += payload;

    // Key schedule through the handshake secrets.
    const std::string shared_hex = x25519::x25519(priv_hex, to_hex(server_pub_raw));
    if (shared_hex.empty()) {
        fail("tls: invalid server key share");
        return -1;
    }
    const std::string zeros(sha384 ? 48 : 32, '\0');  // HashLen zero bytes for Extract
    const std::string early = ks_extract(sha384, std::string(), zeros);
    const std::string derived = derive_secret_impl(early, "derived", "", sha384);
    const std::string hs_secret = ks_extract(sha384, derived, from_hex(shared_hex));
    const std::string c_hs = derive_secret_impl(hs_secret, "c hs traffic", transcript, sha384);
    const std::string s_hs = derive_secret_impl(hs_secret, "s hs traffic", transcript, sha384);
    Keys client_keys = traffic_keys(c_hs, aead, sha384);
    Keys server_keys = traffic_keys(s_hs, aead, sha384);

    // Encrypted handshake flight: EncryptedExtensions, Certificate, CertificateVerify, Finished.
    std::string handshake_bytes;  // decrypted, possibly spanning records
    std::string cert_der;
    std::vector<std::string> cert_chain;  // the full certificate chain (leaf first) for validation
    bool verified_cert = false, server_finished = false;
    bool cert_requested = false;          // server sent CertificateRequest (mail servers do)
    std::string cert_request_context;     // echoed back in our (empty) Certificate reply
    std::string transcript_at_cv, transcript_at_finished;
    // Total-flight cap: the server's handshake messages accumulate into transcript / handshake_bytes /
    // cert_chain BEFORE the certificate is validated, so a hostile (or MITM) server could otherwise
    // stream unbounded records and exhaust memory pre-auth. A real TLS 1.3 flight — even a long cert
    // chain of RSA-4096 leaves — is well under 256 KiB; cap there and fail closed beyond it.
    constexpr std::size_t kMaxHandshakeFlight = std::size_t{256} * 1024;
    std::size_t flight_bytes = 0;
    while (!server_finished) {
        if (!read_record(fd, buffer, rtype, payload)) {
            fail("tls: connection closed during the handshake");
            return -1;
        }
        if (rtype == 20) continue;  // compat ChangeCipherSpec
        if (rtype == 21) {
            fail("tls: server alert during the handshake — " + alert_text(payload));
            return -1;
        }
        if (rtype != 23) {
            fail("tls: unexpected plaintext record during the encrypted handshake");
            return -1;
        }
        unsigned inner_type = 0;
        std::string content;
        if (!open_record(server_keys, payload, inner_type, content)) {
            fail("tls: handshake record failed authentication");
            return -1;
        }
        if (inner_type == 21) {
            fail("tls: server alert during the handshake — " + alert_text(content));
            return -1;
        }
        if (inner_type != 22) {
            fail("tls: unexpected inner record type during the handshake");
            return -1;
        }
        flight_bytes += content.size();
        if (flight_bytes > kMaxHandshakeFlight) {
            fail("tls: server handshake flight too large");
            return -1;
        }
        handshake_bytes += content;

        // Drain complete handshake messages from the reassembly buffer.
        while (handshake_bytes.size() >= 4) {
            const unsigned mtype = static_cast<unsigned char>(handshake_bytes[0]);
            const unsigned mlen = get24(handshake_bytes, 1);
            if (handshake_bytes.size() < 4 + mlen) break;
            const std::string msg = handshake_bytes.substr(0, 4 + mlen);
            handshake_bytes.erase(0, 4 + mlen);

            if (mtype == 11 && msg.size() > 4 + 4 + 3 + 3) {  // Certificate
                // certificate_request_context (1 byte, empty) + the cert list. Walk every
                // CertificateEntry (3-byte length | cert DER | 2-byte extensions | extensions)
                // so the FULL chain is available for path validation, not just the leaf.
                std::size_t i = 4 + 1 + 3;  // header + context length byte + cert-list length
                while (i + 3 <= msg.size()) {
                    const unsigned clen = get24(msg, i);
                    i += 3;
                    if (i + clen > msg.size()) break;
                    cert_chain.push_back(msg.substr(i, clen));
                    i += clen;
                    if (i + 2 > msg.size()) break;
                    i += 2 + get16(msg, i);  // skip the per-certificate extensions
                }
                if (!cert_chain.empty()) cert_der = cert_chain[0];  // the leaf signs CertificateVerify
                transcript_at_cv = transcript + msg;  // transcript THROUGH Certificate
            }
            if (mtype == 13 && msg.size() >= 5) {  // CertificateRequest (optional client auth)
                // RFC 8446 §4.4.2: a client with no certificate MUST still answer with a
                // Certificate message whose certificate_list is empty, echoing this
                // context — smtp.gmail.com requests one and aborts (unexpected_message)
                // on a bare Finished. We never present a certificate; we just decline
                // correctly.
                cert_requested = true;
                const std::size_t ctx_len = static_cast<unsigned char>(msg[4]);
                if (msg.size() >= 5 + ctx_len) cert_request_context = msg.substr(5, ctx_len);
            }
            if (mtype == 15) {  // CertificateVerify
                // cppcheck-suppress stlcstrConstructor  // (ptr,len) subview of msg — not a c_str() copy
                const std::string_view body(msg.data() + 4, msg.size() - 4);
                if (body.size() < 4) {
                    fail("tls: malformed CertificateVerify");
                    return -1;
                }
                const unsigned alg = get16(body, 0);
                const unsigned sig_len = get16(body, 2);
                if (body.size() < 4 + sig_len) {
                    fail("tls: malformed CertificateVerify");
                    return -1;
                }
                // The signed content (same for every algorithm): 64 spaces, the
                // context string, a NUL, then the handshake transcript hash.
                std::string signed_content(64, ' ');
                signed_content += "TLS 1.3, server CertificateVerify";
                signed_content.push_back('\0');
                signed_content += ks_digest(sha384, transcript_at_cv);  // transcript hash = suite hash
                const std::string sig(body.substr(4, sig_len));
                if (alg == 0x0807) {  // ed25519 (signs the message directly)
                    const std::string spki = ed25519_spki_key(cert_der);
                    if (spki.size() != 32) {
                        fail("tls: certificate key is not Ed25519");
                        return -1;
                    }
                    if (!ed25519::verify(to_hex(spki), signed_content, to_hex(sig))) {
                        fail("tls: server CertificateVerify signature is INVALID");
                        return -1;
                    }
                } else if (alg == 0x0403) {  // ecdsa_secp256r1_sha256 (signs SHA-256(content))
                    const std::string point = p256::spki_ec_point(cert_der);
                    if (point.size() != 64) {
                        fail("tls: certificate key is not P-256 EC");
                        return -1;
                    }
                    if (!p256::verify_der(point, hashlib::sha256_digest(signed_content), sig)) {
                        fail("tls: server CertificateVerify (ECDSA P-256) is INVALID");
                        return -1;
                    }
                } else if (alg == 0x0503) {  // ecdsa_secp384r1_sha384 (signs SHA-384(content))
                    // The signature scheme's hash (SHA-384) is independent of the transcript hash
                    // inside signed_content (which is the negotiated suite's hash) — RFC 8446 §4.4.3.
                    const std::string point = p384::spki_ec_point(cert_der);
                    if (point.size() != 96) {
                        fail("tls: certificate key is not P-384 EC");
                        return -1;
                    }
                    if (!p384::verify_der(point, hashlib::sha384_digest(signed_content), sig)) {
                        fail("tls: server CertificateVerify (ECDSA P-384) is INVALID");
                        return -1;
                    }
                } else if (alg == 0x0804) {  // rsa_pss_rsae_sha256 (RSA leaf certificate)
                    // RSA-PSS verifies the signed_content directly (it hashes with SHA-256 internally),
                    // using the RSA public key extracted from the leaf cert's SubjectPublicKeyInfo.
                    if (!rsa::verify_pss_sha256(cert_der, signed_content, sig)) {
                        fail("tls: server CertificateVerify (RSA-PSS SHA-256) is INVALID");
                        return -1;
                    }
                } else {  // LCOV_EXCL_LINE: reached only if a server sends a CertificateVerify whose algorithm ignores our advertised signature_algorithms — a non-conformant peer we don't mirror
                    fail("tls: server certificate uses an algorithm cheatah cannot verify yet "  // LCOV_EXCL_LINE
                         "(Ed25519, ECDSA P-256/P-384, and RSA-PSS SHA-256 are supported) — refusing an "
                         "unauthenticated connection");
                    return -1;
                }
                verified_cert = true;
            }
            if (mtype == 20) {  // Finished
                const std::string finished_key =
                    expand_label_impl(s_hs, "finished", "", sha384 ? 48 : 32, sha384);
                const std::string expect =
                    ks_hmac(sha384, finished_key, ks_digest(sha384, transcript));
                // cppcheck-suppress stlcstrConstructor  // (ptr,len) subview of msg — not a c_str() copy
                const std::string_view got(msg.data() + 4, msg.size() - 4);
                // Constant-time MAC compare (parity with the AEAD tag check): always scan all
                // HashLen bytes of the secret `expect` (32 for SHA-256, 48 for SHA-384), never
                // early-exiting on a mismatching byte, so timing cannot reveal a partial match.
                unsigned char diff = (got.size() == expect.size()) ? 0 : 1;
                for (std::size_t i = 0; i < expect.size(); ++i) {
                    const unsigned char g =
                        i < got.size() ? static_cast<unsigned char>(got[i]) : 0;
                    diff |= g ^ static_cast<unsigned char>(expect[i]);
                }
                if (diff != 0) {
                    fail("tls: server Finished MAC mismatch — handshake transcript tampered");
                    return -1;
                }
                transcript_at_finished = transcript + msg;
                server_finished = true;
            }
            transcript += msg;
        }
    }
    if (!verified_cert) {
        fail("tls: server never proved possession of its certificate key");
        return -1;
    }

    // AUTHENTICATE THE SERVER'S IDENTITY (unless the caller opted out of verification): build the
    // presented chain to a trusted CA, match the hostname against the leaf's SAN, and check the
    // validity dates. Key possession alone (above) does not prove identity — this is what stops an
    // active man-in-the-middle presenting any certificate.
    if (!insecure) {
        x509::TrustStore custom;
        const x509::TrustStore* store = &default_trust();
        if (!ca_file.empty()) {
            custom = x509::load_trust(ca_file);
            store = &custom;
        }
        std::string verr;
        if (!x509::validate(cert_chain, server_name, *store, static_cast<long long>(std::time(nullptr)),
                            verr)) {
            fail("tls: certificate validation failed — " + verr);
            return -1;
        }
    }

    // Application traffic secrets (transcript through server Finished), then OUR Finished
    // (sent under the handshake keys, with the transcript through the server's Finished).
    const std::string derived2 = derive_secret_impl(hs_secret, "derived", "", sha384);
    const std::string master = ks_extract(sha384, derived2, zeros);
    const std::string c_ap = derive_secret_impl(master, "c ap traffic", transcript_at_finished, sha384);
    const std::string s_ap = derive_secret_impl(master, "s ap traffic", transcript_at_finished, sha384);

    // A requested-but-absent client certificate: the empty Certificate reply goes on the
    // wire AND into the transcript BEFORE our Finished (whose MAC covers it) — RFC 8446
    // §4.4.2/§4.4.4. No CertificateVerify follows an empty list.
    if (cert_requested) {
        std::string cert_body;
        cert_body.push_back(static_cast<char>(cert_request_context.size()));
        cert_body += cert_request_context;
        put24(cert_body, 0);  // empty certificate_list
        std::string cert_msg;
        cert_msg.push_back(11);
        put24(cert_msg, static_cast<unsigned>(cert_body.size()));
        cert_msg += cert_body;
        if (!seal_record(fd, client_keys, 22, cert_msg)) {
            fail("tls: cannot send the (empty) client Certificate");
            return -1;
        }
        transcript += cert_msg;
    }

    const std::string c_finished_key =
        expand_label_impl(c_hs, "finished", "", sha384 ? 48 : 32, sha384);
    const std::string verify = ks_hmac(sha384, c_finished_key, ks_digest(sha384, transcript));
    std::string fin_msg;
    fin_msg.push_back(20);
    put24(fin_msg, static_cast<unsigned>(verify.size()));
    fin_msg += verify;
    if (!seal_record(fd, client_keys, 22, fin_msg)) {
        fail("tls: cannot send client Finished");
        return -1;
    }

    Session s;
    s.fd = fd;
    s.client_keys = traffic_keys(c_ap, aead, sha384);
    s.server_keys = traffic_keys(s_ap, aead, sha384);
    s.read_buffer = std::move(buffer);  // bytes already pulled off the socket stay with us

    const std::lock_guard<std::mutex> lock(registry().mutex);
    const long long handle = registry().next_handle++;
    registry().sessions[handle] = std::move(s);
    return handle;
}

} // namespace

/// @cond INTERNAL — the C++-only low-level session API (tls_lowlevel.hpp); cheatah uses the Conn guard
long long client_connect(long long fd, const std::string& server_name, bool insecure,
                         const std::string& ca_file) {
    return handshake(fd, server_name, insecure, ca_file);
}

long long server_accept(long long fd, const std::string& cert_pem, const std::string& key_pem) {
    return server_handshake(fd, cert_pem, key_pem);
}

long long send(long long session, const std::string& data) {
    t_error.clear();
    // Lock ONLY for the map lookup (see recv): the socket write below runs without
    // the global lock so concurrent sessions don't serialize on each other.
    Session* sp = nullptr;
    {
        const std::lock_guard<std::mutex> lock(registry().mutex);
        const auto it = registry().sessions.find(session);
        if (it == registry().sessions.end() || it->second.closed) {
            fail("tls: unknown or closed session");
            return -1;
        }
        sp = &it->second;
    }
    Session& s = *sp;
    // Respect the 16 KiB record plaintext bound.
    std::string_view rest = data;
    while (!rest.empty()) {
        const std::size_t n = std::min<std::size_t>(rest.size(), 16384);
        if (!seal_record(s.fd, s.client_keys, 23, rest.substr(0, n))) {
            fail("tls: send failed");
            return -1;
        }
        rest.remove_prefix(n);
    }
    return 0;
}

std::string recv(long long session, long long bufsize) {
    t_error.clear();
    if (bufsize <= 0) return "";
    // Guard ONLY the map lookup. The per-session buffers (read_buffer/app_pending)
    // and socket are owned by this session's single reader thread, so the blocking
    // record I/O below runs WITHOUT the global lock — otherwise one session's
    // blocking recv would serialize (and at shutdown, starve) every other session's
    // recv/send. A std::map node address is stable until that node is erased, and a
    // session is erased only by its own owner (after this loop), so the pointer is
    // valid for this call. (registry().mutex still serializes find/insert/erase on the map.)
    Session* sp = nullptr;
    {
        const std::lock_guard<std::mutex> lock(registry().mutex);
        const auto it = registry().sessions.find(session);
        if (it == registry().sessions.end()) {
            fail("tls: unknown session");
            return "";
        }
        sp = &it->second;
    }
    Session& s = *sp;
    // Drain up to `bufsize` of application data. We block (in read_record) ONLY while we have
    // nothing to hand back; once app_pending holds data we keep going solely to consume records
    // ALREADY buffered (has_complete_record) — never adding a blocking wait. Because read_record
    // now pulls 64 KiB per recv, one blocking read typically delivers several records, all drained
    // here into a single ≥16 KB return to requests — which keeps the socket drained and the
    // receive window open instead of the old one-record-per-call stall.
    while (!s.closed) {
        if (!s.app_pending.empty() &&
            (s.app_pending.size() >= static_cast<std::size_t>(bufsize) ||
             !has_complete_record(s.read_buffer))) {
            break;  // enough to return, and nothing more ready without blocking
        }
        unsigned rtype = 0;
        std::string payload;
        if (!read_record(s.fd, s.read_buffer, rtype, payload)) {
            s.closed = true;  // peer EOF (or socket timeout) — surfaced as ""
            break;
        }
        if (rtype == 20) continue;       // stray compat ChangeCipherSpec
        if (rtype == 21) {               // plaintext alert (illegal post-handshake, but final)
            s.closed = true;
            break;
        }
        if (rtype != 23) continue;       // ignore anything else
        unsigned inner_type = 0;
        std::string content;
        if (!open_record(s.server_keys, payload, inner_type, content)) {
            fail("tls: record failed authentication");
            s.closed = true;
            break;
        }
        if (inner_type == 23) {
            s.app_pending += content;
        } else if (inner_type == 21) {   // alert ends the stream: close_notify is the
            // normal clean close (surfaced as plain EOF); anything else is the peer
            // REFUSING the session — name it, so a fatal alert never masquerades as EOF.
            if (content.size() != 2 || static_cast<unsigned char>(content[1]) != 0) {
                fail("tls: peer alert — " + alert_text(content));
            }
            s.closed = true;
        } else if (inner_type == 22) {
            // Post-handshake messages: NewSessionTicket(4) is ignored; a KeyUpdate(24)
            // would change the peer's keys — unsupported, so end the stream rather than
            // silently fail to decrypt what follows.
            if (!content.empty() && static_cast<unsigned char>(content[0]) == 24) {
                fail("tls: peer KeyUpdate is not supported");
                s.closed = true;
            }
        }
    }
    const std::size_t n = std::min<std::size_t>(s.app_pending.size(),
                                                static_cast<std::size_t>(bufsize));
    if (n == s.app_pending.size()) {
        std::string out = std::move(s.app_pending);  // whole buffer → move, no copy
        s.app_pending.clear();
        return out;
    }
    std::string out = s.app_pending.substr(0, n);
    s.app_pending.erase(0, n);
    return out;
}

long long close(long long session) {
    t_error.clear();
    const std::lock_guard<std::mutex> lock(registry().mutex);
    const auto it = registry().sessions.find(session);
    if (it == registry().sessions.end()) return -1;
    if (!it->second.closed) {
        const std::string close_notify = {1, 0};  // warning, close_notify
        seal_record(it->second.fd, it->second.client_keys, 21, close_notify);
    }
    registry().sessions.erase(it);
    return 0;
}

long long shutdown(long long session) {
    t_error.clear();
    const std::lock_guard<std::mutex> lock(registry().mutex);
    const auto it = registry().sessions.find(session);
    if (it == registry().sessions.end()) return -1;
    // Wake a reader blocked in recv() WITHOUT erasing the session (that stays the
    // owner's job via close(), after it has joined the reader). Just half-close the
    // socket so the blocking recv returns EOF.
    return socket::shutdown(it->second.fd);
}
/// @endcond

std::string last_error() { return t_error; }

// ---- owning RAII session ----
// Each method forwards to the handle-based free function above; the guard adds deterministic
// close() (close_notify + session erase) on scope exit, so a `with` block cannot leak.

Conn& Conn::operator=(Conn&& other) noexcept {
    if (this != &other) {
        if (session_ > 0) cheatah::tls::close(session_);
        session_ = other.session_;
        other.session_ = 0;
    }
    return *this;
}
Conn::~Conn() {
    if (session_ > 0) cheatah::tls::close(session_);
}
long long Conn::send(const std::string& data) const { return cheatah::tls::send(session_, data); }
std::string Conn::recv(long long bufsize) const { return cheatah::tls::recv(session_, bufsize); }
long long Conn::shutdown() const { return cheatah::tls::shutdown(session_); }
long long Conn::close() {
    if (session_ <= 0) return -1;
    const long long rc = cheatah::tls::close(session_);
    session_ = 0;
    return rc;
}
Conn open(long long fd, const std::string& server_name, bool insecure, const std::string& ca_file) {
    return Conn(client_connect(fd, server_name, insecure, ca_file));
}
Conn accept(long long fd, const std::string& cert_pem, const std::string& key_pem) {
    return Conn(server_handshake(fd, cert_pem, key_pem));
}

namespace detail {
// Cipher preference follows OUR fastest cipher, exactly as OpenSSL/curl do: with AES-NI +
// PCLMULQDQ present, AES-GCM runs at multi-GB/s hardware speed and beats our scalar ChaCha20, so
// offer AES-GCM FIRST; without hardware AES (some VMs/ARM), scalar ChaCha20 is the faster path, so
// lead with it. The server picks from our order when it honors client preference — which is what
// turns a ChaCha-negotiated ~200 MB/s link into a ~320 MB/s AES-GCM one.
//
// Split out and taking the decision as a PARAMETER rather than calling crypto_hardware_active()
// inline, so both orders are reachable from a test on any host. Inline, the branch not matching the
// build machine's CPU was dead code no test could ever execute — the ordering is a wire-format
// decision and deserves to be pinned on every machine, not only on ARM.
void append_cipher_preference(std::string& body, bool hardware_aes) {
    if (hardware_aes) {
        put16(body, 0x1302);         // TLS_AES_256_GCM_SHA384 (hardware AES-NI — preferred)
        put16(body, 0x1301);         // TLS_AES_128_GCM_SHA256 (hardware AES-NI)
        put16(body, 0x1303);         // TLS_CHACHA20_POLY1305_SHA256 (fallback)
    } else {
        put16(body, 0x1303);         // TLS_CHACHA20_POLY1305_SHA256 (no AES-NI — scalar ChaCha wins)
        put16(body, 0x1301);         // TLS_AES_128_GCM_SHA256
        put16(body, 0x1302);         // TLS_AES_256_GCM_SHA384
    }
}


std::string expand_label(std::string_view secret, std::string_view label,
                         std::string_view context, unsigned length) {
    return cheatah::tls::expand_label_impl(secret, label, context, length);
}
std::string derive_secret(std::string_view secret, std::string_view label,
                          std::string_view transcript) {
    return cheatah::tls::derive_secret_impl(secret, label, transcript);
}
bool parse_client_hello(std::string_view msg, std::string& client_pub_raw, unsigned& chosen_suite,
                        std::string& session_id, std::string& sig_algs) {
    return cheatah::tls::parse_client_hello(msg, client_pub_raw, chosen_suite, session_id,
                                            sig_algs);
}
std::string pem_block(const std::string& pem, const std::string& label) {
    return cheatah::tls::pem_block(pem, label);
}
std::vector<std::string> pem_blocks(const std::string& pem, const std::string& label) {
    return cheatah::tls::pem_blocks(pem, label);
}
std::string ed25519_seed_from_pkcs8(std::string_view der) {
    return cheatah::tls::ed25519_seed_from_pkcs8(der);
}
std::string ec_p256_scalar_from_pem(const std::string& key_pem) {
    return cheatah::tls::ec_p256_scalar_from_pem(key_pem);
}
std::string build_client_hello(const std::string& server_name, std::string_view pub_raw) {
    return cheatah::tls::build_client_hello(server_name, pub_raw);
}
} // namespace detail

} // namespace cheatah::tls
