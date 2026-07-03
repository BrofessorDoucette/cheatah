// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "tls.hpp"
#include "tls_lowlevel.hpp"  // the C++-only raw handle API this module implements (+ tls::Conn uses)

#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include <sys/random.h>  // getrandom: client random + ephemeral X25519 key

#include "aead.hpp"     // chacha20poly1305_{en,de}crypt — the record cipher
#include "ed25519.hpp"    // verify — CertificateVerify for Ed25519 server certs
#include "hashlib.hpp"    // sha256_digest, hmac_sha256, hkdf_extract/expand — the key schedule
#include "p256.hpp"       // verify — CertificateVerify for ECDSA P-256 server certs
#include "rsa_verify.hpp" // verify_pss_sha256 — CertificateVerify for RSA (rsa_pss_rsae_sha256) certs
#include "socket.hpp"   // raw fd I/O underneath the record layer
#include "x25519.hpp"   // the key exchange
#include "x509.hpp"     // certificate chain / hostname / expiry validation (server AUTHENTICATION)

// A from-scratch TLS 1.3 client (RFC 8446), cipher suite TLS_CHACHA20_POLY1305_SHA256 only.
// The implementation walks the RFC top to bottom: record layer, transcript hash, the HKDF
// key schedule, then the handshake state machine. Every secret derives through hashlib's
// HKDF; every record seals/opens through the aead module; the ephemeral key is x25519.

namespace cheatah::tls {
namespace {

namespace sock = cheatah::socket;

thread_local std::string t_error;  // last_error() text for this thread

void fail(std::string_view what) { t_error = std::string(what); }

// ---- hex <-> bytes (the crypto modules speak hex for keys) -------------------

std::string to_hex(std::string_view raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (const char ch : raw) {
        out.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    return out;
}

std::string from_hex(std::string_view hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = hex[i + k];
            v <<= 4;
            if (ch >= '0' && ch <= '9') v |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') v |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') v |= static_cast<unsigned>(ch - 'A' + 10);  // LCOV_EXCL_LINE: from_hex is fed only lowercase x25519 hex; the uppercase branch is defensive parity
        }
        out.push_back(static_cast<char>(v));
    }
    return out;
}

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

// HKDF-Expand-Label(secret, label, context, length) with the "tls13 " prefix.
std::string expand_label_impl(std::string_view secret, std::string_view label,
                              std::string_view context, unsigned length) {
    std::string info;
    put16(info, length);
    info.push_back(static_cast<char>(6 + label.size()));
    info += "tls13 ";
    info += label;
    info.push_back(static_cast<char>(context.size()));
    info += context;
    return hashlib::hkdf_expand(secret, info, length);
}

// Derive-Secret(secret, label, transcript) = Expand-Label(secret, label, SHA-256(transcript), 32).
std::string derive_secret_impl(std::string_view secret, std::string_view label,
                               std::string_view transcript) {
    return expand_label_impl(secret, label, hashlib::sha256_digest(transcript), 32);
}

namespace {  // resume the file-local helpers

// One traffic direction: AEAD key + iv + record sequence number.
struct Keys {
    std::string key_hex;   // AEAD key (hex): 32 bytes for ChaCha20-Poly1305, 16 for AES-128-GCM
    std::string iv;        // 12-byte raw iv; per-record nonce = iv XOR seq
    std::uint64_t seq = 0;
    bool aes_gcm = false;  // record cipher: false = ChaCha20-Poly1305 (0x1303), true = AES-128-GCM (0x1301)
};

// Both suites use the SHA-256 key schedule and a 12-byte iv; only the record-key length differs
// (AES-128 = 16 bytes, ChaCha20 = 32). aes_gcm selects which record cipher seal/open_record use.
Keys traffic_keys(std::string_view secret, bool aes_gcm) {
    Keys k;
    k.aes_gcm = aes_gcm;
    k.key_hex = to_hex(expand_label_impl(secret, "key", "", aes_gcm ? 16 : 32));
    k.iv = expand_label_impl(secret, "iv", "", 12);
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

std::mutex g_mutex;
std::map<long long, Session> g_sessions;
long long g_next_handle = 1;

// ---- record I/O ---------------------------------------------------------------

// Read exactly one TLS record (header + payload) from the socket into (type, payload).
// Blocking, bounded by the fd's socket timeout. False on EOF/short read.
bool read_record(long long fd, std::string& buffer, unsigned& type, std::string& payload) {
    while (buffer.size() < 5) {
        const std::string chunk = sock::recv(fd, 16384);
        if (chunk.empty()) return false;
        buffer += chunk;
    }
    type = static_cast<unsigned char>(buffer[0]);
    const unsigned len = get16(buffer, 3);
    if (len > 16384 + 256) {  // RFC bound + AEAD overhead: anything bigger is malformed
        return false;
    }
    while (buffer.size() < 5 + len) {
        const std::string chunk = sock::recv(fd, 16384);
        if (chunk.empty()) return false;
        buffer += chunk;
    }
    payload = buffer.substr(5, len);
    buffer.erase(0, 5 + len);
    return true;
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
    const std::string ct = k.aes_gcm
                               ? aead::aes128gcm_encrypt(k.key_hex, nonce_hex(k), aad, inner)
                               : aead::chacha20poly1305_encrypt(k.key_hex, nonce_hex(k), aad, inner);
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
    std::string inner = k.aes_gcm
                            ? aead::aes128gcm_decrypt(k.key_hex, nonce_hex(k), aad, payload)
                            : aead::chacha20poly1305_decrypt(k.key_hex, nonce_hex(k), aad, payload);
    ++k.seq;
    if (inner.empty() && payload.size() > 16) return false;  // tag mismatch (or empty record)
    while (!inner.empty() && inner.back() == '\0') inner.pop_back();  // strip padding
    if (inner.empty()) return false;  // a record must carry a content type
    inner_type = static_cast<unsigned char>(inner.back());
    content = inner.substr(0, inner.size() - 1);
    return true;
}

// ---- handshake construction -----------------------------------------------------

std::string random_bytes(std::size_t n) {
    std::string out(n, '\0');
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = getrandom(out.data() + got, n - got, 0);
        if (r <= 0) return "";  // the kernel CSPRNG failing is fatal for key material
        got += static_cast<std::size_t>(r);
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
    put16(body, 4);                  // cipher_suites: two suites (4 bytes)
    put16(body, 0x1303);             // TLS_CHACHA20_POLY1305_SHA256 (preferred)
    put16(body, 0x1301);             // TLS_AES_128_GCM_SHA256 (some hosts only do AES-GCM)
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
        put16(a, 6);
        put16(a, 0x0807);  // ed25519
        put16(a, 0x0804);  // rsa_pss_rsae_sha256 (verified — see rsa_verify.hpp)
        put16(a, 0x0403);  // ecdsa_secp256r1_sha256 (verified — see p256)
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
    if (suite != 0x1303 && suite != 0x1301) return false;  // must be one of the suites we offered
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
    const bool aes_gcm = (chosen_suite == 0x1301);  // else 0x1303 ChaCha20-Poly1305
    transcript += payload;

    // Key schedule through the handshake secrets.
    const std::string shared_hex = x25519::x25519(priv_hex, to_hex(server_pub_raw));
    if (shared_hex.empty()) {
        fail("tls: invalid server key share");
        return -1;
    }
    const std::string zeros(32, '\0');
    const std::string early = hashlib::hkdf_extract(std::string(), zeros);
    const std::string derived = derive_secret_impl(early, "derived", "");
    const std::string hs_secret = hashlib::hkdf_extract(derived, from_hex(shared_hex));
    const std::string c_hs = derive_secret_impl(hs_secret, "c hs traffic", transcript);
    const std::string s_hs = derive_secret_impl(hs_secret, "s hs traffic", transcript);
    Keys client_keys = traffic_keys(c_hs, aes_gcm);
    Keys server_keys = traffic_keys(s_hs, aes_gcm);

    // Encrypted handshake flight: EncryptedExtensions, Certificate, CertificateVerify, Finished.
    std::string handshake_bytes;  // decrypted, possibly spanning records
    std::string cert_der;
    std::vector<std::string> cert_chain;  // the full certificate chain (leaf first) for validation
    bool verified_cert = false, server_finished = false;
    std::string transcript_at_cv, transcript_at_finished;
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
                signed_content += hashlib::sha256_digest(transcript_at_cv);
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
                } else if (alg == 0x0804) {  // rsa_pss_rsae_sha256 (RSA leaf certificate)
                    // RSA-PSS verifies the signed_content directly (it hashes with SHA-256 internally),
                    // using the RSA public key extracted from the leaf cert's SubjectPublicKeyInfo.
                    if (!rsa::verify_pss_sha256(cert_der, signed_content, sig)) {
                        fail("tls: server CertificateVerify (RSA-PSS SHA-256) is INVALID");
                        return -1;
                    }
                } else {  // LCOV_EXCL_LINE: reached only if a server sends a CertificateVerify whose algorithm ignores our advertised signature_algorithms — a non-conformant peer we don't mirror
                    fail("tls: server certificate uses an algorithm cheatah cannot verify yet "  // LCOV_EXCL_LINE
                         "(Ed25519, ECDSA P-256, and RSA-PSS SHA-256 are supported) — refusing an "
                         "unauthenticated connection");
                    return -1;
                }
                verified_cert = true;
            }
            if (mtype == 20) {  // Finished
                const std::string finished_key = expand_label_impl(s_hs, "finished", "", 32);
                const std::string expect =
                    hashlib::hmac_sha256(finished_key, hashlib::sha256_digest(transcript));
                // cppcheck-suppress stlcstrConstructor  // (ptr,len) subview of msg — not a c_str() copy
                const std::string_view got(msg.data() + 4, msg.size() - 4);
                // Constant-time MAC compare (parity with the AEAD tag check): always scan all
                // 32 bytes of the secret `expect`, never early-exiting on a mismatching byte, so
                // timing cannot reveal a partial match of the handshake MAC.
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
    const std::string derived2 = derive_secret_impl(hs_secret, "derived", "");
    const std::string master = hashlib::hkdf_extract(derived2, zeros);
    const std::string c_ap = derive_secret_impl(master, "c ap traffic", transcript_at_finished);
    const std::string s_ap = derive_secret_impl(master, "s ap traffic", transcript_at_finished);

    const std::string c_finished_key = expand_label_impl(c_hs, "finished", "", 32);
    const std::string verify =
        hashlib::hmac_sha256(c_finished_key, hashlib::sha256_digest(transcript));
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
    s.client_keys = traffic_keys(c_ap, aes_gcm);
    s.server_keys = traffic_keys(s_ap, aes_gcm);
    s.read_buffer = std::move(buffer);  // bytes already pulled off the socket stay with us

    const std::lock_guard<std::mutex> lock(g_mutex);
    const long long handle = g_next_handle++;
    g_sessions[handle] = std::move(s);
    return handle;
}

} // namespace

/// @cond INTERNAL — the C++-only low-level session API (tls_lowlevel.hpp); cheatah uses the Conn guard
long long client_connect(long long fd, const std::string& server_name, bool insecure,
                         const std::string& ca_file) {
    return handshake(fd, server_name, insecure, ca_file);
}

long long send(long long session, const std::string& data) {
    t_error.clear();
    // Lock ONLY for the map lookup (see recv): the socket write below runs without
    // the global lock so concurrent sessions don't serialize on each other.
    Session* sp = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_sessions.find(session);
        if (it == g_sessions.end() || it->second.closed) {
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
    // valid for this call. (g_mutex still serializes find/insert/erase on the map.)
    Session* sp = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_sessions.find(session);
        if (it == g_sessions.end()) {
            fail("tls: unknown session");
            return "";
        }
        sp = &it->second;
    }
    Session& s = *sp;
    while (s.app_pending.empty() && !s.closed) {
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
        } else if (inner_type == 21) {   // alert: close_notify (or fatal) ends the stream
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
    const std::string out = s.app_pending.substr(0, n);
    s.app_pending.erase(0, n);
    return out;
}

long long close(long long session) {
    t_error.clear();
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_sessions.find(session);
    if (it == g_sessions.end()) return -1;
    if (!it->second.closed) {
        const std::string close_notify = {1, 0};  // warning, close_notify
        seal_record(it->second.fd, it->second.client_keys, 21, close_notify);
    }
    g_sessions.erase(it);
    return 0;
}

long long shutdown(long long session) {
    t_error.clear();
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_sessions.find(session);
    if (it == g_sessions.end()) return -1;
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
long long Conn::send(const std::string& data) { return cheatah::tls::send(session_, data); }
std::string Conn::recv(long long bufsize) { return cheatah::tls::recv(session_, bufsize); }
long long Conn::shutdown() { return cheatah::tls::shutdown(session_); }
long long Conn::close() {
    if (session_ <= 0) return -1;
    const long long rc = cheatah::tls::close(session_);
    session_ = 0;
    return rc;
}
Conn open(long long fd, const std::string& server_name, bool insecure, const std::string& ca_file) {
    return Conn(client_connect(fd, server_name, insecure, ca_file));
}

namespace detail {
std::string expand_label(std::string_view secret, std::string_view label,
                         std::string_view context, unsigned length) {
    return cheatah::tls::expand_label_impl(secret, label, context, length);
}
std::string derive_secret(std::string_view secret, std::string_view label,
                          std::string_view transcript) {
    return cheatah::tls::derive_secret_impl(secret, label, transcript);
}
} // namespace detail

} // namespace cheatah::tls
