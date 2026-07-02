// System tests for the `tls` module: a REAL TLS 1.3 handshake against `openssl s_server`
// (test infrastructure only — the client side is pure cheatah crypto: x25519, ChaCha20-
// Poly1305, HKDF, Ed25519 verification). Also the refusal paths: a non-TLS peer, and a
// closed port.
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "e2e_harness.hpp"

#include "socket.hpp"
#include "tls.hpp"
#include "tls_lowlevel.hpp"  // this C++ test drives the raw handle API (hidden from cheatah)

namespace sock = cheatah::socket;
namespace tls = cheatah::tls;

namespace {

// Generate a throwaway self-signed cert (@p newkey selects the key algorithm — "ed25519",
// "rsa:2048", "ec" …) and start `openssl s_server` on @p port restricted to @p ciphersuites.
// Returns true when the server is accepting. Killed via pkill in stop(). This is what lets the
// system tests exercise each leaf-cert algorithm (Ed25519 / RSA-PSS / ECDSA P-256) and each record
// cipher (ChaCha20-Poly1305 / AES-128-GCM) against a real TLS 1.3 peer.
// @complexity O(1) (two subprocesses)  @alloc the command strings  @test TlsSys (helper)
class OpensslServer {
public:
    explicit OpensslServer(long long port, const std::string& newkey = "ed25519",
                           const std::string& ciphersuites = "TLS_CHACHA20_POLY1305_SHA256")
        : port_(port),
          cert_(std::string(PURR_TEST_TMP) + "/tls_test_cert.pem"),
          key_(std::string(PURR_TEST_TMP) + "/tls_test_key.pem") {
        const std::string gen = "openssl req -x509 -newkey " + newkey + " -keyout '" + key_ +
                                "' -out '" + cert_ +
                                "' -days 2 -nodes -subj /CN=localhost 2>/dev/null";
        ok_ = std::system(gen.c_str()) == 0;
        if (!ok_) return;
        const std::string serve = "openssl s_server -accept " + std::to_string(port_) +
                                  " -cert '" + cert_ + "' -key '" + key_ +
                                  "' -tls1_3 -ciphersuites " + ciphersuites + " -www "
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
    const long long s = tls::client_connect(fd, "localhost");
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
    tls::Conn conn = tls::open(tcp.fd(), "localhost");
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
bool handshake_gets_200(long long port) {
    const long long fd = sock::tcp_connect("127.0.0.1", port);
    if (fd < 0) return false;
    sock::set_timeout(fd, 5000);
    const long long s = tls::client_connect(fd, "localhost");
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
    EXPECT_TRUE(handshake_gets_200(server.port())) << tls::last_error();
}

// AES-128-GCM record cipher (Ed25519 cert isolates the cipher): exercises the AES-128-GCM
// seal_record/open_record path on a real TLS 1.3 channel.
TEST(TlsSys, HandshakeAes128Gcm) {
    OpensslServer server(47942, "ed25519", "TLS_AES_128_GCM_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port())) << tls::last_error();
}

// RSA leaf cert AND AES-128-GCM together — the exact combination required to reach exchanges whose
// stream endpoints serve an RSA chain over an AES-GCM-only cipher policy (both additions at once).
TEST(TlsSys, HandshakeRsaAndAes128Gcm) {
    OpensslServer server(47943, "rsa:2048", "TLS_AES_128_GCM_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port())) << tls::last_error();
}

// ECDSA P-256 leaf certificate: exercises the ecdsa_secp256r1_sha256 CertificateVerify path.
TEST(TlsSys, HandshakeEcdsaP256Certificate) {
    OpensslServer server(47944, "ec -pkeyopt ec_paramgen_curve:prime256v1",
                         "TLS_CHACHA20_POLY1305_SHA256");
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    EXPECT_TRUE(handshake_gets_200(server.port())) << tls::last_error();
}

// No common cipher suite (server offers ONLY AES-256-GCM, which cheatah does not implement): the
// handshake MUST fail rather than silently proceed, and the error must NAME the alert — exercising the
// alert-code diagnostic so a "no common cipher" refusal reports a named reason, not a generic error.
TEST(TlsSys, RefusesUnsupportedCipherWithNamedAlert) {
    OpensslServer server(47945, "ed25519", "TLS_AES_256_GCM_SHA384");
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

// THE FINALE: a pure-cheatah https GET — requests.purr (HTTP in .purr) over the tls module
// (from-scratch TLS 1.3) over the cheatah crypto modules, against a real TLS server.
TEST(TlsSys, HttpsGet) {
    OpensslServer server(47933);
    ASSERT_TRUE(server.ok()) << "could not start openssl s_server (test infrastructure)";
    const std::string src = "import requests\nimport io\n"
                            "let o = requests.Options({.timeout_ms = 5000})\n"
                            "let r = requests.get(\"https://127.0.0.1:" +
                            std::to_string(server.port()) +
                            "/\", o)\nio.print(r.status)\nio.print(r.ok())\n"
                            "io.print(len(r.body) > 0)\n";
    e2e::expect_e2e("requests_https_get", src, "200\nTrue\nTrue\n");
}
