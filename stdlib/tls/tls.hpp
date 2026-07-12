// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file tls.hpp
 * @brief cheatah `tls` — a from-scratch TLS 1.3 CLIENT (RFC 8446). `import tls` to use it.
 *        Built ENTIRELY on the cheatah crypto modules: `x25519` key exchange, the `aead`
 *        ChaCha20-Poly1305 record cipher, and hashlib's HKDF key schedule. No OpenSSL.
 *
 * Scope (v1): TLS 1.3 only, cipher suite TLS_CHACHA20_POLY1305_SHA256, X25519 key share,
 * SNI. The handshake transcript is fully verified (server Finished MAC), and the server's
 * CertificateVerify signature is checked for the common leaf-certificate key types: Ed25519
 * (cheatah's `ed25519`), ECDSA P-256 (`p256`) and P-384 (`p384`), and RSA via
 * rsa_pss_rsae_sha256 (`rsa_verify.hpp`). Servers using any OTHER certificate algorithm are
 * REFUSED with a clear error rather than silently left unauthenticated — no unverified
 * connections. The presented chain then gets full X.509 path validation (hostname, validity
 * window, signatures up to a trusted CA — see x509.hpp) unless explicitly opted out.
 *
 * Sessions ride an already-connected TCP fd (cheatah `socket`). The cheatah-facing API is the
 * owning `tls::Conn` guard, created by `tls.open(fd, server_name)`: it sends close_notify and
 * erases the session automatically when it goes out of scope (so a cheatah program cannot leak a
 * session). The underlying fd is NOT owned by the session — close it with `socket.close()` (or
 * guard it with a `socket::Conn`). The flat handle-based calls live in tls_lowlevel.hpp (C++ only).
 */
#include <string>
#include <string_view>

namespace cheatah::tls {

// The low-level, handle-based API (client_connect / send / recv / close / shutdown, keyed by an
// integer session id) is C++-only and lives in tls_lowlevel.hpp. It is intentionally NOT part of
// this cheatah-facing header: a cheatah program cannot reach it, so it cannot leak a session — it
// uses the owning `tls::Conn` guard + `tls.open()` below, which release automatically at scope
// exit. `tls::Conn` is implemented on top of that low-level API.

/**
 * The most recent tls error message on this thread ("" when none).
 *
 * @return the last error text set on the calling thread, or "" if none.
 * @complexity O(1).
 * @alloc the returned string.
 * @test TlsSys.RefusesBadPeer
 * @crtest TlsSys.HandshakeAgainstOpenssl
 * @systest TlsSys.HttpsGet
 */
std::string last_error();

// ---- owning RAII session (the `with`-friendly, leak-proof API) ----

/**
 * @brief An owning TLS 1.3 client session — closes (and erases) itself on destruction.
 *
 * The RAII counterpart to the handle-based calls above. A `Conn` owns one session; when it
 * is destroyed (scope exit out of a `with` body, including via exception) or explicitly
 * close()d, the session is torn down and removed from the module's session table, so
 * `with tls.open(sock.fd(), host) as conn { … }` cannot leak the session. Move-only: the
 * copy operations are deleted and a moved-from `Conn` is left closed. The underlying TCP fd
 * is NOT owned here (tls rides a caller-owned socket) — guard it with a socket::Conn.
 */
class Conn {
public:
    /**
     * Construct a closed session (owns nothing).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahTls.ConnDefaultIsClosed
     */
    Conn() = default;
    /**
     * Adopt an existing session handle (e.g. from client_connect()); the `Conn` now owns it.
     * @param session a session handle to take ownership of (<= 0 for a closed session).
     * @complexity O(1).
     * @alloc none.
     * @test TlsSys.ConnGuardRoundTrip
     */
    explicit Conn(long long session) : session_(session) {}
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    /**
     * Move-construct, taking over @p other's session (the moved-from `Conn` becomes closed).
     * @param other the session to move from.
     * @complexity O(1).
     * @alloc none.
     * @test TlsSys.ConnGuardRoundTrip
     */
    Conn(Conn&& other) noexcept : session_(other.session_) { other.session_ = 0; }
    /**
     * Move-assign, closing this session first, then taking over @p other's (which becomes closed).
     * @param other the session to move from.
     * @return reference to this session.
     * @complexity O(1).
     * @alloc none.
     * @test TlsSys.ConnGuardRoundTrip
     */
    Conn& operator=(Conn&& other) noexcept;
    /**
     * Send close_notify and forget the session if still open.
     * @complexity O(log n) lookup + a close_notify write.
     * @alloc a small close_notify record (when still open).
     * @test TlsSys.ConnGuardRoundTrip
     */
    ~Conn();

    /**
     * Is a session open?
     * @return true iff this owns an open session.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahTls.ConnDefaultIsClosed
     */
    bool is_open() const { return session_ > 0; }
    /**
     * The raw session handle (for the low-level calls).
     * @return the owned handle, or 0 when closed.
     * @complexity O(1).
     * @alloc none.
     * @test TlsSys.ConnGuardRoundTrip
     */
    long long id() const { return session_; }
    /**
     * Encrypt and send @p data as TLS application data (see the free send()).
     * @param data plaintext to send.
     * @return 0 on success, -1 on error.
     * @complexity O(|data|).
     * @alloc the ciphertext record(s).
     * @test TlsSys.ConnGuardRoundTrip
     */
    long long send(const std::string& data);
    /**
     * Receive and decrypt up to @p bufsize bytes of application data (see the free recv()).
     * @param bufsize maximum plaintext bytes to return.
     * @return the plaintext, or "" on clean close/EOF/error.
     * @complexity O(record size).
     * @alloc the returned plaintext.
     * @test TlsSys.ConnGuardRoundTrip
     */
    std::string recv(long long bufsize);
    /**
     * Wake a reader blocked in recv() WITHOUT closing the session (see the free shutdown()).
     * @return 0 on success, -1 on error.
     * @complexity O(log n) lookup + one syscall.
     * @alloc none.
     * @test TlsSys.ConnGuardRoundTrip
     */
    long long shutdown();
    /**
     * Close the session now (idempotent — the destructor will not close it again).
     * @return 0 on success, -1 if already closed / unknown.
     * @complexity O(log n) lookup + a close_notify write.
     * @alloc a small close_notify record (when still open).
     * @test TlsSys.ConnGuardRoundTrip
     */
    long long close();

private:
    long long session_ = 0;
};

/**
 * Run the TLS 1.3 client handshake over connected fd @p fd and return an owning Conn (the
 * RAII, `with`-friendly form of client_connect()). By default the server is AUTHENTICATED:
 * the certificate chain is built to a trusted CA, the hostname is matched against the leaf's
 * subjectAltName, and the validity dates are checked — so the connection resists an active MITM.
 * @param fd a CONNECTED TCP socket (e.g. socket::Conn::fd()).
 * @param server_name the hostname (SNI + certificate SAN match).
 * @param insecure when true, skip chain/hostname/expiry validation (leaf-key possession only) —
 *        for a pinned/controlled peer where identity is established out of band. Default false.
 * @param ca_file a PEM CA bundle to trust instead of the system store (empty = system default).
 * @return an owning Conn; on handshake or validation failure its is_open() is false (see last_error()).
 * @complexity one network round trip + O(handshake bytes) crypto (+ a one-time CA-store parse).
 * @alloc the session state.
 * @test TlsSys.ConnGuardRoundTrip
 * @systest TlsSys.HttpsGet
 */
Conn open(long long fd, const std::string& server_name, bool insecure = false,
          const std::string& ca_file = "");

/**
 * Run the TLS 1.3 SERVER handshake over an accepted TCP fd and return an owning Conn — the
 * `with`-friendly server counterpart to open(). We present @p cert_pem and prove possession of
 * its key by signing the handshake, so a client that validates the certificate gets an
 * authenticated, encrypted channel with **no OpenSSL** anywhere.
 *
 * The server certificate must be **Ed25519** (the leaf's key and the private key both Ed25519);
 * this is the from-scratch signing path cheatah owns end to end. Both suites (ChaCha20-Poly1305
 * and AES-128-GCM) and X25519 key exchange are supported; the client picks the suite.
 * @param fd a CONNECTED TCP socket from socket::accept()/Listener (e.g. one client of a server loop).
 * @param cert_pem the server's leaf certificate, PEM (`-----BEGIN CERTIFICATE-----`).
 * @param key_pem the matching PKCS#8 Ed25519 private key, PEM (`-----BEGIN PRIVATE KEY-----`).
 * @return an owning Conn; on handshake failure its is_open() is false (see last_error()).
 * @complexity one network round trip + O(handshake bytes) crypto.
 * @alloc the session state.
 * @systest TlsSys.ServerHandshakeAgainstOpenssl
 */
Conn accept(long long fd, const std::string& cert_pem, const std::string& key_pem);

// Internal key-schedule primitives, exposed for the RFC 8448 vector tests only.
namespace detail {
/**
 * HKDF-Expand-Label (RFC 8446 §7.1). @complexity O(length) @alloc the returned string
 * @test CheatahTls.KeySchedule
 */
std::string expand_label(std::string_view secret, std::string_view label,
                         std::string_view context, unsigned length);
/**
 * Derive-Secret (RFC 8446 §7.1). @complexity O(1) @alloc the returned string
 * @test CheatahTls.KeySchedule
 */
std::string derive_secret(std::string_view secret, std::string_view label,
                          std::string_view transcript);

// Server-handshake parsers, exposed as test seams so crafted-input unit tests can drive every
// refusal branch deterministically (no network peer needed). Not part of the cheatah surface.
/**
 * Parse a ClientHello (choose a supported suite, extract the client X25519 share + session id).
 * @param msg the handshake message bytes. @param client_pub_raw filled with the 32-byte share.
 * @param chosen_suite filled with the negotiated cipher suite. @param session_id filled with the
 * legacy_session_id to echo. @return true iff usable (TLS 1.3, X25519, a shared suite).
 * @test CheatahTls.ParseClientHelloRejectsMalformed
 */
bool parse_client_hello(std::string_view msg, std::string& client_pub_raw, unsigned& chosen_suite,
                        std::string& session_id);
/**
 * A PEM block's DER bytes (strict base64). @param pem the PEM text. @param label e.g.
 * "CERTIFICATE". @return the decoded DER, or "" when the block is absent/malformed.
 * @test CheatahTls.PemBlockExtractsAndRejects
 */
std::string pem_block(const std::string& pem, const std::string& label);
/**
 * The 32-byte Ed25519 seed from a PKCS#8 private-key DER. @param der the key DER.
 * @return the 32-byte seed, or "" when @p der is not a PKCS#8 Ed25519 key.
 * @test CheatahTls.PemBlockExtractsAndRejects
 */
std::string ed25519_seed_from_pkcs8(std::string_view der);
/**
 * Build a ClientHello handshake message (offering both suites + an X25519 key share) — the test
 * seam a crafted "malformed client" peer uses to drive the server handshake past ServerHello.
 * @param server_name the SNI host. @param pub_raw a 32-byte X25519 client share.
 * @return the ClientHello message bytes.
 * @systest TlsSys.ServerRejectsMidHandshake
 */
std::string build_client_hello(const std::string& server_name, std::string_view pub_raw);
} // namespace detail

} // namespace cheatah::tls
