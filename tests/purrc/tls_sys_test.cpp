// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System tests for the `tls` module: a REAL TLS 1.3 handshake against `openssl s_server`
// (test infrastructure only — the client side is pure cheatah crypto: x25519, ChaCha20-
// Poly1305, HKDF, Ed25519 verification). Also the refusal paths: a non-TLS peer, and a
// closed port.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>  // access/X_OK — probe for a Homebrew openssl on macOS

#include <gtest/gtest.h>

#include "e2e_harness.hpp"

#include "socket.hpp"
#include "tls.hpp"
#include "tls_lowlevel.hpp"  // this C++ test drives the raw handle API (hidden from cheatah)

namespace sock = cheatah::socket;
namespace tls = cheatah::tls;

namespace {

// Resolve the `openssl` CLI used purely as test infrastructure (the peer). On Linux the
// system openssl is real OpenSSL; on macOS /usr/bin/openssl is LibreSSL, whose s_server/req
// flag surface differs (e.g. -newkey ed25519, -ciphersuites), so prefer a Homebrew OpenSSL.
// Overridable via $CHEATAH_OPENSSL for unusual layouts.
const std::string& openssl_bin() {
    static const std::string bin = [] () -> std::string {
        if (const char* env = std::getenv("CHEATAH_OPENSSL"); env && *env) return env;
#if defined(__APPLE__)
        for (const char* cand : {"/opt/homebrew/opt/openssl@3/bin/openssl",
                                 "/usr/local/opt/openssl@3/bin/openssl",
                                 "/opt/homebrew/bin/openssl",
                                 "/usr/local/bin/openssl"}) {
            if (::access(cand, X_OK) == 0) return cand;
        }
#endif
        return "openssl";  // real OpenSSL on PATH (Linux); on macOS a LibreSSL fallback
    }();
    return bin;
}

// Generate a throwaway self-signed cert (@p newkey selects the key algorithm — "ed25519",
// "rsa:2048", "ec" …) and start `openssl s_server` on @p port restricted to @p ciphersuites.
// Returns true when the server is accepting. Killed via pkill in stop(). This is what lets the
// system tests exercise each leaf-cert algorithm (Ed25519 / RSA-PSS / ECDSA P-256) and each record
// cipher (ChaCha20-Poly1305 / AES-128-GCM) against a real TLS 1.3 peer.
// @complexity O(1) (two subprocesses)  @alloc the command strings  @test TlsSys (helper)
class OpensslServer {
public:
    // request_client_cert adds `-verify 1`, which makes s_server send a CertificateRequest
    // and ACCEPT a client that declines (unlike -Verify, which demands one). That is exactly
    // the peer that used to break us: smtp.gmail.com asks, we do not present a certificate,
    // and the handshake must still complete.
    explicit OpensslServer(long long port, const std::string& newkey = "ed25519",
                           const std::string& ciphersuites = "TLS_CHACHA20_POLY1305_SHA256",
                           bool request_client_cert = false)
        // Per-port cert/key paths so concurrent TlsSys tests (ctest -j) don't clobber each other's
        // files — the client now VALIDATES the cert, so a shared path would race into failures.
        : port_(port),
          cert_(std::string(PURR_TEST_TMP) + "/tls_test_cert_" + std::to_string(port) + ".pem"),
          key_(std::string(PURR_TEST_TMP) + "/tls_test_key_" + std::to_string(port) + ".pem") {
        // Self-signed, with a subjectAltName so it is a valid trust anchor for "localhost": passed
        // to the client as a ca_file (or via $SSL_CERT_FILE) it authenticates itself.
        const std::string gen = openssl_bin() + " req -x509 -newkey " + newkey + " -keyout '" + key_ +
                                "' -out '" + cert_ +
                                "' -days 2 -nodes -subj /CN=localhost "
                                "-addext subjectAltName=DNS:localhost 2>/dev/null";
        ok_ = std::system(gen.c_str()) == 0;
        if (!ok_) return;
        const std::string serve = openssl_bin() + " s_server -accept " + std::to_string(port_) +
                                  " -cert '" + cert_ + "' -key '" + key_ +
                                  "' -tls1_3 -ciphersuites " + ciphersuites + " -www " +
                                  (request_client_cert ? "-verify 1 " : "") +
                                  ">/dev/null 2>&1 &";
        ok_ = std::system(serve.c_str()) == 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));  // let it bind
    }
    ~OpensslServer() {
        const std::string kill = "pkill -f 's_server -accept " + std::to_string(port_) + "'";
        std::system(kill.c_str());
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] long long port() const { return port_; }
    [[nodiscard]] const std::string& cert_path() const { return cert_; }

private:
    long long port_;
    std::string cert_, key_;
    bool ok_ = false;
};

}  // namespace

// The full handshake: X25519 exchange, transcript-verified Finished, Ed25519 CertificateVerify,
// then an HTTP exchange over the encrypted channel (s_server -www answers GET with 200).
TEST(TlsSys, HandshakeAgainstOpenssl) {
    OpensslServer server(47931);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const long long fd = sock::tcp_connect("127.0.0.1", server.port());
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "localhost", false, server.cert_path());
    ASSERT_GE(s, 0) << tls::last_error();

    ASSERT_EQ(tls::send(s, "GET / HTTP/1.0\r\n\r\n"), 0) << tls::last_error();
    std::string all;
    for (;;) {
        const std::string chunk = tls::recv(s, 65536);
        if (chunk.empty()) break;
        all += chunk;
    }
    EXPECT_EQ(all.compare(0, 15, "HTTP/1.0 200 ok"), 0) << all.substr(0, 60);
    tls::close(s);
    sock::close(fd);
}

// The owning-guard round trip: socket::Conn + tls::Conn (the `with`-friendly RAII API) run the
// same X25519 handshake + encrypted GET, but the fd and TLS session are released deterministically
// by the guards' destructors. Exercises tls::open/Conn::send/recv/shutdown/close/is_open/id and
// both move operations end-to-end against a real peer.
TEST(TlsSys, ConnGuardRoundTrip) {
    OpensslServer server(47960);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    sock::Conn tcp = sock::open("127.0.0.1", server.port());
    ASSERT_TRUE(tcp.is_open()) << sock::last_error();
    tcp.set_timeout(5000);
    tls::Conn conn = tls::open(tcp.fd(), "localhost", false, server.cert_path());
    ASSERT_TRUE(conn.is_open()) << tls::last_error();
    EXPECT_GT(conn.id(), 0);

    tls::Conn active(std::move(conn));   // move-construct: transfer ownership
    EXPECT_FALSE(conn.is_open());
    ASSERT_EQ(active.send("GET / HTTP/1.0\r\n\r\n"), 0) << tls::last_error();
    std::string all;
    for (;;) {
        const std::string chunk = active.recv(65536);
        if (chunk.empty()) break;
        all += chunk;
    }
    EXPECT_EQ(all.compare(0, 15, "HTTP/1.0 200 ok"), 0) << all.substr(0, 60);

    tls::Conn sink;
    sink = std::move(active);            // move-assign onto a closed guard
    EXPECT_FALSE(active.is_open());
    EXPECT_EQ(sink.shutdown(), 0);
    EXPECT_EQ(sink.close(), 0);
    EXPECT_EQ(sink.close(), -1);         // idempotent
    // `tcp` (socket::Conn) closes the fd via its destructor here — no leak.
}

namespace {
// Connect to a local TLS 1.3 server, GET /, and return true iff it answered 200 over the encrypted
// channel — i.e. the FULL cheatah handshake (key exchange + leaf-cert verify + record cipher) and an
// encrypted round trip all succeeded. The per-cert/per-cipher system tests below assert on this.
bool handshake_gets_200(long long port, const std::string& ca_file) {
    const long long fd = sock::tcp_connect("127.0.0.1", port);
    if (fd < 0) return false;
    sock::set_timeout(fd, 5000);
    // Verify the server against its own self-signed cert (SAN localhost) supplied as the CA.
    const long long s = tls::client_connect(fd, "localhost", false, ca_file);
    if (s < 0) {
        sock::close(fd);
        return false;
    }
    tls::send(s, "GET / HTTP/1.0\r\n\r\n");
    std::string all;
    for (;;) {
        const std::string c = tls::recv(s, 65536);
        if (c.empty()) break;
        all += c;
        if (all.size() > 40) break;
    }
    tls::close(s);
    sock::close(fd);
    return all.compare(0, 12, "HTTP/1.0 200") == 0;
}
}  // namespace

// RSA leaf certificate (ChaCha20 isolates the cert path): exercises RSA-PSS (rsa_pss_rsae_sha256)
// CertificateVerify — the cheatah TLS client proving an RSA server's key possession.
TEST(TlsSys, HandshakeRsaCertificate) {
    OpensslServer server(47941, "rsa:2048", "TLS_CHACHA20_POLY1305_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// A server that ASKS for a client certificate. RFC 8446 §4.4.2: a client with nothing to present
// must still answer the CertificateRequest with a Certificate message carrying an empty
// certificate_list and echoing the request's context — a bare Finished is an unexpected_message and
// the peer aborts. We used to send the bare Finished, so any such server was unreachable;
// smtp.gmail.com is one, which is how this surfaced.
//
// `-verify 1` requests without requiring, so a correct decline still reaches the HTTP exchange:
// a 200 here proves the client both sent the empty Certificate AND folded it into the transcript
// (get either wrong and the server's Finished check fails instead).
TEST(TlsSys, DeclinesCertificateRequest) {
    OpensslServer server(47951, "ed25519", "TLS_CHACHA20_POLY1305_SHA256", true);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// AES-128-GCM record cipher (Ed25519 cert isolates the cipher): exercises the AES-128-GCM
// seal_record/open_record path on a real TLS 1.3 channel.
TEST(TlsSys, HandshakeAes128Gcm) {
    OpensslServer server(47942, "ed25519", "TLS_AES_128_GCM_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// RSA leaf cert AND AES-128-GCM together — the exact combination required to reach exchanges whose
// stream endpoints serve an RSA chain over an AES-GCM-only cipher policy (both additions at once).
TEST(TlsSys, HandshakeRsaAndAes128Gcm) {
    OpensslServer server(47943, "rsa:2048", "TLS_AES_128_GCM_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// ECDSA P-256 leaf certificate: exercises the ecdsa_secp256r1_sha256 CertificateVerify path.
TEST(TlsSys, HandshakeEcdsaP256Certificate) {
    OpensslServer server(47944, "ec -pkeyopt ec_paramgen_curve:prime256v1",
                         "TLS_CHACHA20_POLY1305_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// ECDSA P-384 leaf certificate: exercises the ecdsa_secp384r1_sha384 CertificateVerify path
// and, via the self-signed cert's own signature, the x509 P-384 chain-verification arm.
TEST(TlsSys, HandshakeEcdsaP384Certificate) {
    OpensslServer server(47946, "ec -pkeyopt ec_paramgen_curve:secp384r1",
                         "TLS_CHACHA20_POLY1305_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// TLS_AES_256_GCM_SHA384: exercises the SHA-384 key schedule + AES-256-GCM record cipher end-to-end
// against openssl as the reference peer, with an Ed25519 leaf (isolates the suite from the cert path).
TEST(TlsSys, HandshakeAes256GcmSha384) {
    OpensslServer server(47947, "ed25519", "TLS_AES_256_GCM_SHA384");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// The hardest combination: an ECDSA P-384 leaf UNDER the TLS_AES_256_GCM_SHA384 suite — the P-384
// CertificateVerify (0x0503) + the P-384 x509 chain arm + the SHA-384 key schedule + AES-256-GCM
// records all exercised together in one handshake.
TEST(TlsSys, HandshakeEcdsaP384AndAes256Sha384) {
    OpensslServer server(47948, "ec -pkeyopt ec_paramgen_curve:secp384r1",
                         "TLS_AES_256_GCM_SHA384");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port(), server.cert_path())) << tls::last_error();
}

// No common cipher suite (server offers ONLY AES-128-CCM, which cheatah does not implement): the
// handshake MUST fail rather than silently proceed, and the error must NAME the alert — exercising the
// alert-code diagnostic so a "no common cipher" refusal reports a named reason, not a generic error.
TEST(TlsSys, RefusesUnsupportedCipherWithNamedAlert) {
    OpensslServer server(47945, "ed25519", "TLS_AES_128_CCM_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const long long fd = sock::tcp_connect("127.0.0.1", server.port());
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "localhost");
    EXPECT_LT(s, 0);  // no shared suite -> the handshake must fail
    EXPECT_NE(tls::last_error().find("handshake_failure"), std::string::npos) << tls::last_error();
    sock::close(fd);
}

// A peer that speaks plaintext garbage must fail the handshake with a clear error.
TEST(TlsSys, RefusesBadPeer) {
    const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(listen_fd, 0);
    const long long port = sock::local_port(listen_fd);
    std::thread peer([listen_fd]() {
        const long long client = sock::accept(listen_fd);
        sock::sendall(client, "definitely not a TLS server\r\n");
        sock::close(client);
    });
    const long long fd = sock::tcp_connect("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 2000);
    const long long s = tls::client_connect(fd, "localhost");
    EXPECT_LT(s, 0);
    EXPECT_FALSE(tls::last_error().empty());
    sock::close(fd);
    peer.join();
    sock::close(listen_fd);
}

// A server that, right after the client's ClientHello, sends a plaintext TLS alert record instead of
// a ServerHello must fail the handshake with a NAMED alert. This is adversarial crafted-byte input
// (a 7-byte record: content_type 21 || version 0303 || length 0002 || level || description) — NOT a
// mirrored handshake — and it drives alert_text()'s human-readable name table over every RFC 8446
// alert description the client recognizes, so a refusal reports a specific cause, not a bare code.
TEST(TlsSys, NamesEveryHandshakeAlert) {
    // description code -> the exact substring alert_text() must place in last_error().
    const std::vector<std::pair<int, std::string>> alerts = {
        {0, "close_notify"},          {10, "unexpected_message"},
        {20, "bad_record_mac"},       {22, "record_overflow"},
        {42, "bad_certificate"},      {43, "unsupported_certificate"},
        {47, "illegal_parameter"},    {48, "unknown_ca"},
        {49, "access_denied"},        {50, "decode_error"},
        {51, "decrypt_error"},        {70, "protocol_version"},
        {71, "insufficient_security"},{80, "internal_error"},
        {109, "missing_extension"},   {110, "unsupported_extension"},
        {112, "unrecognized_name"},   {116, "certificate_required"},
        {120, "no_application_protocol"},
        {200, "unknown"},             // an unlisted code -> the default "unknown" name
    };
    for (const auto& [code, name] : alerts) {
        const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
        ASSERT_GE(listen_fd, 0);
        const long long port = sock::local_port(listen_fd);
        std::thread peer([listen_fd, code]() {
            const long long client = sock::accept(listen_fd);
            if (client < 0) return;
            sock::recv(client, 16384);  // drain the ClientHello, then answer with a fatal alert
            // Build the 7-byte alert record explicitly — a "\x00" in a string literal would
            // terminate it early, so append each byte (type 21 || ver 0303 || len 0002 || lvl || desc).
            std::string alert;
            alert.push_back(static_cast<char>(21));                 // content_type = alert
            alert.push_back(static_cast<char>(0x03));               // legacy record version 0x0303
            alert.push_back(static_cast<char>(0x03));
            alert.push_back(static_cast<char>(0x00));               // length = 2
            alert.push_back(static_cast<char>(0x02));
            alert.push_back(static_cast<char>(2));                  // level = fatal
            alert.push_back(static_cast<char>(code & 0xFF));        // description
            sock::sendall(client, alert);
            sock::close(client);
        });
        const long long fd = sock::tcp_connect("127.0.0.1", port);
        ASSERT_GE(fd, 0);
        sock::set_timeout(fd, 2000);
        const long long s = tls::client_connect(fd, "localhost");
        EXPECT_LT(s, 0) << "an alert instead of ServerHello must fail the handshake (code " << code << ")";
        EXPECT_NE(tls::last_error().find(name), std::string::npos)
            << "alert " << code << " should be named '" << name << "': " << tls::last_error();
        sock::close(fd);
        peer.join();
        sock::close(listen_fd);
    }
}

// A short (1-byte) alert record must not misparse: alert_text reports "(empty alert)" rather than
// reading a description byte that is not there — the p.size() < 2 guard.
TEST(TlsSys, ShortAlertRecordIsHandled) {
    const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(listen_fd, 0);
    const long long port = sock::local_port(listen_fd);
    std::thread peer([listen_fd]() {
        const long long client = sock::accept(listen_fd);
        if (client < 0) return;
        sock::recv(client, 16384);
        std::string alert;  // an alert record with length=1 (truncated body: level only, no desc)
        alert.push_back(static_cast<char>(21));    // content_type = alert
        alert.push_back(static_cast<char>(0x03));  // legacy record version 0x0303
        alert.push_back(static_cast<char>(0x03));
        alert.push_back(static_cast<char>(0x00));  // length = 1
        alert.push_back(static_cast<char>(0x01));
        alert.push_back(static_cast<char>(2));     // one byte only (level, no description)
        sock::sendall(client, alert);
        sock::close(client);
    });
    const long long fd = sock::tcp_connect("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 2000);
    const long long s = tls::client_connect(fd, "localhost");
    EXPECT_LT(s, 0);
    EXPECT_NE(tls::last_error().find("(empty alert)"), std::string::npos) << tls::last_error();
    sock::close(fd);
    peer.join();
    sock::close(listen_fd);
}

// Verify-by-default REFUSES an untrusted (self-signed, unknown-CA) server: this is the MITM
// defense — key possession alone no longer completes the handshake. Exercises the default
// (system) trust store + the validation-failure path.
TEST(TlsSys, VerifyRejectsUntrustedServer) {
    OpensslServer server(47934);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const long long fd = sock::tcp_connect("127.0.0.1", server.port());
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "localhost");  // no ca_file -> system store only
    EXPECT_LT(s, 0);
    EXPECT_NE(tls::last_error().find("certificate validation failed"), std::string::npos)
        << tls::last_error();
    sock::close(fd);
}

// Verify-by-default REFUSES a certificate whose SAN does not match the requested hostname (even
// though the cert is otherwise trusted via ca_file).
TEST(TlsSys, VerifyRejectsWrongHostname) {
    OpensslServer server(47935);  // SAN = localhost
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const long long fd = sock::tcp_connect("127.0.0.1", server.port());
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "wrong.example", false, server.cert_path());
    EXPECT_LT(s, 0);
    EXPECT_NE(tls::last_error().find("host"), std::string::npos) << tls::last_error();
    sock::close(fd);
}

// insecure=true (a pinned/controlled peer) skips validation: the same untrusted, wrong-hostname
// server that verification refuses is accepted.
TEST(TlsSys, InsecureSkipsValidation) {
    OpensslServer server(47936);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const long long fd = sock::tcp_connect("127.0.0.1", server.port());
    ASSERT_GE(fd, 0);
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "wrong.example", true, "");  // insecure -> no checks
    ASSERT_GE(s, 0) << tls::last_error();
    tls::close(s);
    sock::close(fd);
}

// THE FINALE: a pure-cheatah https GET — requests.purr (HTTP in .purr) over the tls module
// (from-scratch TLS 1.3) over the cheatah crypto modules, against a real TLS server, WITH full
// certificate validation (the cert's own PEM as the trusted CA; host "localhost" matches its SAN).
TEST(TlsSys, HttpsGet) {
    OpensslServer server(47933);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const std::string src = "import requests\nimport io\n"
                            "let o = requests.Options({.timeout_ms = 5000, .ca_file = \"" +
                            server.cert_path() + "\"})\n"
                            "let r = requests.get(\"https://localhost:" +
                            std::to_string(server.port()) +
                            "/\", o)\nio.print(r.status_code)\nio.print(r.ok())\n"
                            "io.print(len(r.body) > 0)\n";
    e2e::expect_e2e("requests_https_get", src, "200\nTrue\nTrue\n");
}

namespace {
std::string slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string out;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    static_cast<void>(std::fclose(f));
    return out;
}

// Run `openssl s_client` against our server and return its stdout. `-verify_return_error` +
// `-CAfile <our cert>` makes OpenSSL — the reference implementation — FAIL unless our from-scratch
// server handshake (ServerHello, key schedule, Certificate, Ed25519 CertificateVerify, Finished)
// is byte-correct and the cert validates. So a passing assertion is OpenSSL certifying our server.
std::string run_s_client(long long port, const std::string& ca_path, const std::string& request) {
    const std::string cmd =
        "printf '" + request + "' | openssl s_client -connect 127.0.0.1:" + std::to_string(port) +
        " -tls1_3 -CAfile '" + ca_path + "' -verify_return_error -servername localhost -quiet 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}
}  // namespace

// The MIRROR of HandshakeAgainstOpenssl: now the SERVER is pure cheatah and OpenSSL is the client.
// A cheatah TLS server (tls::server_accept, Ed25519 cert) accepts one connection, and `openssl
// s_client -verify_return_error` completes the TLS 1.3 handshake and reads our reply — so OpenSSL
// validates our ServerHello + certificate flight + Ed25519 CertificateVerify + Finished end to end.
// HTTPS with zero non-cheatah software on the server side.
TEST(TlsSys, ServerHandshakeAgainstOpenssl) {
    const long long port = 47971;
    const std::string dir = PURR_TEST_TMP;
    const std::string cert = dir + "/tls_srv_cert_" + std::to_string(port) + ".pem";
    const std::string key = dir + "/tls_srv_key_" + std::to_string(port) + ".pem";
    const std::string gen = "openssl req -x509 -newkey ed25519 -keyout '" + key + "' -out '" +
                            cert + "' -days 2 -nodes -subj /CN=localhost "
                            "-addext subjectAltName=DNS:localhost 2>/dev/null";
    ASSERT_EQ(std::system(gen.c_str()), 0) << "could not generate an Ed25519 cert (test infra)";
    const std::string cert_pem = slurp(cert), key_pem = slurp(key);
    ASSERT_FALSE(cert_pem.empty());
    ASSERT_FALSE(key_pem.empty());

    const long long listen_fd = sock::tcp_listen("127.0.0.1", port, 4);
    ASSERT_GE(listen_fd, 0) << sock::last_error();

    // Server thread: accept ONE client, run the cheatah TLS server handshake through the owning
    // `tls::accept` guard (the leak-safe API a cheatah program uses), and answer its GET.
    std::string srv_err;
    std::thread server([&] {
        const long long conn = sock::accept(listen_fd);
        if (conn < 0) { srv_err = "accept failed"; return; }
        sock::set_timeout(conn, 5000);
        tls::Conn tc = tls::accept(conn, cert_pem, key_pem);   // owning guard, closes at scope exit
        if (!tc.is_open()) { srv_err = tls::last_error(); sock::close(conn); return; }
        tc.recv(4096);  // drain the client's request line
        const std::string body = "hello from a pure-cheatah TLS server";
        tc.send("HTTP/1.0 200 ok\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
        sock::close(conn);  // tc closes the TLS session via its destructor here
    });

    const std::string out = run_s_client(port, cert, "GET / HTTP/1.0\\r\\n\\r\\n");
    server.join();
    sock::close(listen_fd);

    EXPECT_TRUE(srv_err.empty()) << "cheatah server: " << srv_err;
    EXPECT_NE(out.find("hello from a pure-cheatah TLS server"), std::string::npos)
        << "openssl s_client output:\n" << out;
}

// The ECDSA mirror of ServerHandshakeAgainstOpenssl: the SAME cheatah server code presenting a
// P-256 leaf — the certificate type public CAs actually issue — with OpenSSL validating our
// ecdsa_secp256r1_sha256 CertificateVerify end to end. This is the browser-facing HTTPS shape.
TEST(TlsSys, ServerHandshakeEcdsaAgainstOpenssl) {
    const long long port = 47972;
    const std::string dir = PURR_TEST_TMP;
    const std::string cert = dir + "/tls_srv_ec_cert_" + std::to_string(port) + ".pem";
    const std::string key = dir + "/tls_srv_ec_key_" + std::to_string(port) + ".pem";
    const std::string gen = "openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
                            "-keyout '" + key + "' -out '" + cert +
                            "' -days 2 -nodes -subj /CN=localhost "
                            "-addext subjectAltName=DNS:localhost 2>/dev/null";
    ASSERT_EQ(std::system(gen.c_str()), 0) << "could not generate a P-256 cert (test infra)";
    const std::string cert_pem = slurp(cert), key_pem = slurp(key);
    ASSERT_FALSE(cert_pem.empty());
    ASSERT_FALSE(key_pem.empty());

    const long long listen_fd = sock::tcp_listen("127.0.0.1", port, 4);
    ASSERT_GE(listen_fd, 0) << sock::last_error();
    std::string srv_err;
    std::thread server([&] {
        const long long conn = sock::accept(listen_fd);
        if (conn < 0) { srv_err = "accept failed"; return; }
        sock::set_timeout(conn, 5000);
        tls::Conn tc = tls::accept(conn, cert_pem, key_pem);
        if (!tc.is_open()) { srv_err = tls::last_error(); sock::close(conn); return; }
        tc.recv(4096);
        const std::string body = "hello from a pure-cheatah ECDSA TLS server";
        tc.send("HTTP/1.0 200 ok\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
        sock::close(conn);
    });

    const std::string out = run_s_client(port, cert, "GET / HTTP/1.0\\r\\n\\r\\n");
    server.join();
    sock::close(listen_fd);

    EXPECT_TRUE(srv_err.empty()) << "cheatah server: " << srv_err;
    EXPECT_NE(out.find("hello from a pure-cheatah ECDSA TLS server"), std::string::npos)
        << "openssl s_client output:\n" << out;
}

// A CA-signed leaf served as a fullchain.pem (leaf + intermediate in one file — the exact artifact
// Let's Encrypt/acme.sh hand a production server): the Certificate message must carry EVERY block,
// because s_client is given only the CA. A leaf-only emission (the old behavior) cannot validate.
TEST(TlsSys, ServerHandshakeFullChainAgainstOpenssl) {
    const long long port = 47973;
    const std::string dir = PURR_TEST_TMP;
    const std::string ca_key = dir + "/tls_chain_ca_key.pem", ca_cert = dir + "/tls_chain_ca.pem";
    const std::string leaf_key = dir + "/tls_chain_leaf_key.pem";
    const std::string leaf_csr = dir + "/tls_chain_leaf.csr";
    const std::string leaf_cert = dir + "/tls_chain_leaf.pem";
    const std::string ext = dir + "/tls_chain_ext.cnf";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
                           "-keyout '" + ca_key + "' -out '" + ca_cert +
                           "' -days 2 -nodes -subj /CN=cheatah-test-ca 2>/dev/null").c_str()), 0);
    ASSERT_EQ(std::system(("openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 "
                           "-keyout '" + leaf_key + "' -out '" + leaf_csr +
                           "' -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    {
        std::FILE* f = std::fopen(ext.c_str(), "w");
        ASSERT_NE(f, nullptr);
        static_cast<void>(std::fputs("subjectAltName=DNS:localhost\n", f));
        static_cast<void>(std::fclose(f));
    }
    ASSERT_EQ(std::system(("openssl x509 -req -in '" + leaf_csr + "' -CA '" + ca_cert +
                           "' -CAkey '" + ca_key + "' -CAcreateserial -days 2 -extfile '" + ext +
                           "' -out '" + leaf_cert + "' 2>/dev/null").c_str()), 0);
    const std::string fullchain = slurp(leaf_cert) + slurp(ca_cert);  // leaf first, then issuer
    const std::string key_pem = slurp(leaf_key);
    ASSERT_FALSE(key_pem.empty());

    const long long listen_fd = sock::tcp_listen("127.0.0.1", port, 4);
    ASSERT_GE(listen_fd, 0) << sock::last_error();
    std::string srv_err;
    std::thread server([&] {
        const long long conn = sock::accept(listen_fd);
        if (conn < 0) { srv_err = "accept failed"; return; }
        sock::set_timeout(conn, 5000);
        tls::Conn tc = tls::accept(conn, fullchain, key_pem);
        if (!tc.is_open()) { srv_err = tls::last_error(); sock::close(conn); return; }
        tc.recv(4096);
        const std::string body = "hello through a full chain";
        tc.send("HTTP/1.0 200 ok\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
        sock::close(conn);
    });

    // s_client trusts ONLY the CA — validating proves the intermediate rode in our Certificate.
    const std::string out = run_s_client(port, ca_cert, "GET / HTTP/1.0\\r\\n\\r\\n");
    server.join();
    sock::close(listen_fd);

    EXPECT_TRUE(srv_err.empty()) << "cheatah server: " << srv_err;
    EXPECT_NE(out.find("hello through a full chain"), std::string::npos)
        << "openssl s_client output:\n" << out;
}

// The server's certificate/key pre-flight refusals — checked before any socket read, so a bad fd
// is never touched. A malformed cert PEM, an unparseable key, a cert/key MISMATCH (each key type),
// and a key on an unsupported curve each fail fast with a named error rather than starting a
// doomed handshake.
TEST(TlsSys, ServerRejectsBadCredentials) {
    const std::string dir = PURR_TEST_TMP;
    // A valid Ed25519 cert + key to mix and match against bad ones.
    const std::string ed_cert = dir + "/tls_bad_ed_cert.pem", ed_key = dir + "/tls_bad_ed_key.pem";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ed25519 -keyout '" + ed_key + "' -out '" +
                           ed_cert + "' -days 2 -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    const std::string ed_cert_pem = slurp(ed_cert), ed_key_pem = slurp(ed_key);
    // A P-256 pair for the cross mismatches, and a P-384 key for the unsupported-curve refusal.
    const std::string ec_cert = dir + "/tls_bad_ec_cert.pem", ec_key = dir + "/tls_bad_ec_key.pem";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -keyout '" +
                           ec_key + "' -out '" + ec_cert +
                           "' -days 2 -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    const std::string ec_cert_pem = slurp(ec_cert), ec_key_pem = slurp(ec_key);
    const std::string p384_key = dir + "/tls_bad_p384_key.pem";
    ASSERT_EQ(std::system(("openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-384 -out '" +
                           p384_key + "' 2>/dev/null").c_str()), 0);

    EXPECT_LT(tls::server_accept(-1, "not a certificate", ed_key_pem), 0);   // malformed cert PEM
    EXPECT_FALSE(tls::last_error().empty());
    EXPECT_LT(tls::server_accept(-1, ed_cert_pem, "not a key"), 0);          // unparseable key
    EXPECT_LT(tls::server_accept(-1, ec_cert_pem, ed_key_pem), 0);   // Ed25519 key, P-256 cert
    EXPECT_NE(tls::last_error().find("does not match"), std::string::npos) << tls::last_error();
    EXPECT_LT(tls::server_accept(-1, ed_cert_pem, ec_key_pem), 0);   // P-256 key, Ed25519 cert
    EXPECT_NE(tls::last_error().find("does not match"), std::string::npos) << tls::last_error();
    EXPECT_LT(tls::server_accept(-1, ec_cert_pem, slurp(p384_key)), 0);      // unsupported curve
}

// The server's ClientHello refusals against crafted TCP peers (mirror of RefusesBadPeer for the
// client): a peer that closes immediately, one that sends a non-handshake record, and one that
// sends a type-22 record with a junk body. Each must fail server_accept with a non-empty error.
TEST(TlsSys, ServerRejectsBadClientHello) {
    const std::string dir = PURR_TEST_TMP;
    const std::string cert = dir + "/tls_srvrej_cert.pem", key = dir + "/tls_srvrej_key.pem";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ed25519 -keyout '" + key + "' -out '" + cert +
                           "' -days 2 -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    const std::string cert_pem = slurp(cert), key_pem = slurp(key);

    // (record bytes to send, "" = just close). type-21 alert record, and a type-22 junk record.
    std::string alert;  // content_type 21, ver 0303, len 2, body
    alert.push_back(21); alert.push_back(0x03); alert.push_back(0x03);
    alert.push_back(0x00); alert.push_back(0x02); alert.push_back(2); alert.push_back(40);
    std::string junk_hs;  // content_type 22, ver 0303, len 4, garbage that isn't a ClientHello
    junk_hs.push_back(22); junk_hs.push_back(0x03); junk_hs.push_back(0x03);
    junk_hs.push_back(0x00); junk_hs.push_back(0x04);
    junk_hs.append(4, static_cast<char>(0xEE));
    const std::vector<std::string> payloads = {"", alert, junk_hs};

    for (const std::string& p : payloads) {
        const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
        ASSERT_GE(listen_fd, 0);
        const long long port = sock::local_port(listen_fd);
        std::string err;
        std::thread server([&] {
            const long long conn = sock::accept(listen_fd);
            if (conn < 0) return;
            sock::set_timeout(conn, 2000);
            const long long s = tls::server_accept(conn, cert_pem, key_pem);
            if (s < 0) err = tls::last_error();
            else tls::close(s);
            sock::close(conn);
        });
        const long long fd = sock::tcp_connect("127.0.0.1", port);
        ASSERT_GE(fd, 0);
        if (!p.empty()) sock::sendall(fd, p);
        sock::close(fd);
        server.join();
        sock::close(listen_fd);
        EXPECT_FALSE(err.empty()) << "server should have refused this ClientHello";
    }
}

namespace {
// Wrap raw handshake bytes in a TLS plaintext record of the given content type.
std::string tls_record(unsigned type, const std::string& body) {
    std::string r;
    r.push_back(static_cast<char>(type));
    r.push_back(0x03);
    r.push_back(0x03);
    r.push_back(static_cast<char>((body.size() >> 8) & 0xFF));
    r.push_back(static_cast<char>(body.size() & 0xFF));
    r += body;
    return r;
}
}  // namespace

// RFC 8446 §4.4.3: the server must not sign with an algorithm the client did not offer. A crafted
// client sends a ClientHello that is valid in every respect (TLS 1.3, X25519 share, a shared
// suite) but omits signature_algorithms entirely — the one shape openssl will never produce — and
// the server must refuse before its certificate flight rather than sign anyway.
TEST(TlsSys, ServerRefusesClientWithoutOurSignatureAlgorithm) {
    const std::string dir = PURR_TEST_TMP;
    const std::string cert = dir + "/tls_sigalg_cert.pem", key = dir + "/tls_sigalg_key.pem";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ed25519 -keyout '" + key + "' -out '" + cert +
                           "' -days 2 -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    const std::string cert_pem = slurp(cert), key_pem = slurp(key);

    // A minimal, well-formed ClientHello with NO extension 13.
    std::string body;
    const auto be16 = [&](std::string& o, unsigned v) {
        o.push_back(static_cast<char>((v >> 8) & 0xFF));
        o.push_back(static_cast<char>(v & 0xFF));
    };
    be16(body, 0x0303);
    body.append(32, 'R');          // random
    body.push_back(0);             // empty legacy_session_id
    be16(body, 2);
    be16(body, 0x1303);            // one suite: ChaCha20-Poly1305
    body.push_back(1);
    body.push_back(0);             // null compression
    std::string ext;
    be16(ext, 43); be16(ext, 3); ext.push_back(2); be16(ext, 0x0304);  // supported_versions
    {
        std::string entry;
        be16(entry, 0x001d); be16(entry, 32); entry.append(32, 'K');
        std::string ks; be16(ks, static_cast<unsigned>(entry.size())); ks += entry;
        be16(ext, 51); be16(ext, static_cast<unsigned>(ks.size())); ext += ks;
    }
    be16(body, static_cast<unsigned>(ext.size()));
    body += ext;
    std::string hello;
    hello.push_back(1);
    hello.push_back(0); be16(hello, static_cast<unsigned>(body.size()));  // 24-bit length
    hello += body;

    const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(listen_fd, 0);
    const long long port = sock::local_port(listen_fd);
    std::string err;
    std::thread server([&] {
        const long long conn = sock::accept(listen_fd);
        if (conn < 0) return;
        sock::set_timeout(conn, 2000);
        const long long s = tls::server_accept(conn, cert_pem, key_pem);
        if (s < 0) err = tls::last_error();
        else tls::close(s);
        sock::close(conn);
    });
    const long long fd = sock::tcp_connect("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    sock::sendall(fd, tls_record(22, hello));
    sock::close(fd);
    server.join();
    sock::close(listen_fd);
    EXPECT_NE(err.find("signature_algorithms"), std::string::npos)
        << "expected the §4.4.3 refusal, got: " << err;
}

// A crafted client that sends a WELL-FORMED ClientHello (so the server proceeds through ServerHello
// and its whole encrypted flight) and then misbehaves — closing, or sending an alert / a wrong-type
// record / a bogus "encrypted" record where the client Finished belongs. Each drives one of the
// server's post-ServerHello refusal branches (EOF, client alert, unexpected record, failed record
// authentication). A zero X25519 share additionally drives the invalid-key-share refusal.
TEST(TlsSys, ServerRejectsMidHandshake) {
    const std::string dir = PURR_TEST_TMP;
    const std::string cert = dir + "/tls_srvmid_cert.pem", key = dir + "/tls_srvmid_key.pem";
    ASSERT_EQ(std::system(("openssl req -x509 -newkey ed25519 -keyout '" + key + "' -out '" + cert +
                           "' -days 2 -nodes -subj /CN=localhost 2>/dev/null").c_str()), 0);
    const std::string cert_pem = slurp(cert), key_pem = slurp(key);

    const std::string good_share(32, 'K');          // any valid u-coordinate
    const std::string zero_share(32, '\0');         // low-order point -> x25519 yields no secret
    const std::string ch = tls::detail::build_client_hello("localhost", good_share);

    enum Mode { CLOSE, ALERT, WRONG_TYPE, BOGUS_AEAD, ZERO_KEY };
    for (int m = CLOSE; m <= ZERO_KEY; ++m) {
        const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
        ASSERT_GE(listen_fd, 0);
        const long long port = sock::local_port(listen_fd);
        std::string err;
        std::thread server([&] {
            const long long conn = sock::accept(listen_fd);
            if (conn < 0) return;
            sock::set_timeout(conn, 2000);
            const long long s = tls::server_accept(conn, cert_pem, key_pem);
            if (s < 0) err = tls::last_error();
            else tls::close(s);
            sock::close(conn);
        });
        const long long fd = sock::tcp_connect("127.0.0.1", port);
        ASSERT_GE(fd, 0);
        if (m == ZERO_KEY) {
            sock::set_timeout(fd, 2000);
            sock::sendall(fd, tls_record(22, tls::detail::build_client_hello("localhost", zero_share)));
        } else {
            sock::sendall(fd, tls_record(22, ch));   // a valid ClientHello: server sends its flight
            // Drain the WHOLE flight (a short read timeout returns "" once the server has sent it
            // all and is blocked reading our Finished) — so the server reaches its client-Finished
            // read and our misbehavior below lands there, not on an early send.
            sock::set_timeout(fd, 300);
            while (!sock::recv(fd, 16384).empty()) { /* keep draining */ }
            sock::set_timeout(fd, 2000);
            if (m == ALERT) {
                std::string a;
                a.push_back(2);
                a.push_back(40);                      // fatal handshake_failure
                sock::sendall(fd, tls_record(21, a));
            } else if (m == WRONG_TYPE) {
                sock::sendall(fd, tls_record(22, std::string(4, 'x')));  // plaintext where 23 is due
            } else if (m == BOGUS_AEAD) {
                sock::sendall(fd, tls_record(23, std::string(64, 'Z')));  // fails AEAD authentication
            }
            // CLOSE: send nothing more.
        }
        sock::close(fd);
        server.join();
        sock::close(listen_fd);
        EXPECT_FALSE(err.empty()) << "server should refuse mid-handshake (mode " << m << ")";
    }
}
