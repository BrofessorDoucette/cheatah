// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System tests for the `websocket` module: a REAL RFC 6455 WebSocket handshake +
// echo against Node's `ws` library (the reference WebSocket implementation) behind
// Node's built-in TLS — test infrastructure only. The client side is pure cheatah:
// the from-scratch tls 1.3 client (x25519, ChaCha20-Poly1305, HKDF, leaf-cert
// verify) plus this module's from-scratch RFC 6455 framing. No WebSocket or TLS
// protocol code is mirrored in our tree — `ws` frames, Node does TLS, openssl mints
// the cert. This mirrors tls_sys_test.cpp's OpensslServer pattern.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "socket.hpp"
#include "websocket.hpp"
#include "websocket_lowlevel.hpp"  // this C++ test drives the raw handle API (hidden from cheatah)

namespace sock = cheatah::socket;
namespace ws = cheatah::websocket;

#ifndef PURR_TEST_TMP
#define PURR_TEST_TMP "."
#endif
#ifndef WEBSOCKET_FIXTURE_DIR
#define WEBSOCKET_FIXTURE_DIR "."
#endif
#ifndef NODE_EXECUTABLE
#define NODE_EXECUTABLE "node"
#endif

namespace {

// Generate a throwaway self-signed leaf cert (@p newkey selects the key algorithm —
// "ec -pkeyopt ec_paramgen_curve:prime256v1", "rsa:2048", "ed25519") and launch the
// Node `ws` wss echo server (tests/fixtures/wss_echo_server.js) on @p port. The echo
// server is the reference RFC 6455 peer; cheatah's client is what's under test. Waits
// for the server's "READY" marker (bound), then is pkill'd in the destructor.
// @complexity O(1) (two subprocesses)  @alloc the command strings + a ready-file path
class WssEchoServer {
public:
    explicit WssEchoServer(long long port,
                           const std::string& newkey = "ec -pkeyopt ec_paramgen_curve:prime256v1",
                           const std::string& mode = "")
        : port_(port) {
        const std::string tmp = PURR_TEST_TMP;
        const std::string tag = std::to_string(port_);
        cert_ = tmp + "/ws_test_cert_" + tag + ".pem";
        key_ = tmp + "/ws_test_key_" + tag + ".pem";
        ready_ = tmp + "/ws_test_ready_" + tag + ".log";
        std::remove(ready_.c_str());
        // Self-signed with a SAN so the cheatah client can authenticate it as its own trust
        // anchor (passed as ca_file) — the TLS client now validates the certificate by default.
        const std::string gen = "openssl req -x509 -newkey " + newkey + " -keyout '" + key_ +
                                "' -out '" + cert_ +
                                "' -days 2 -nodes -subj /CN=localhost "
                                "-addext subjectAltName=DNS:localhost 2>/dev/null";
        cert_ok_ = std::system(gen.c_str()) == 0;
        if (!cert_ok_) return;
        const std::string script = std::string(WEBSOCKET_FIXTURE_DIR) + "/wss_echo_server.js";
        const std::string serve = "'" + std::string(NODE_EXECUTABLE) + "' '" + script + "' '" +
                                  cert_ + "' '" + key_ + "' " + tag + " " + mode + " >'" +
                                  ready_ + "' 2>&1 &";
        node_ok_ = std::system(serve.c_str()) == 0;
        // Wait (up to ~5s) for the server to print READY, i.e. it has bound the port.
        for (int i = 0; i < 100; ++i) {
            if (ready_marker_seen()) { bound_ = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    ~WssEchoServer() {
        const std::string kill =
            "pkill -f 'wss_echo_server.js .* " + std::to_string(port_) + "'";
        std::system(kill.c_str());
    }
    [[nodiscard]] bool ok() const { return cert_ok_ && node_ok_ && bound_; }
    [[nodiscard]] long long port() const { return port_; }
    [[nodiscard]] const std::string& cert_path() const { return cert_; }
    [[nodiscard]] std::string url() const {
        return "wss://localhost:" + std::to_string(port_) + "/";
    }

private:
    [[nodiscard]] bool ready_marker_seen() const {
        std::FILE* f = std::fopen(ready_.c_str(), "rb");
        if (f == nullptr) return false;
        char buf[256];
        const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[n] = '\0';
        return std::string(buf).find("READY") != std::string::npos;
    }
    long long port_;
    std::string cert_, key_, ready_;
    bool cert_ok_ = false, node_ok_ = false, bound_ = false;
};

// node + the ws library must be present: the websocket handshake/framing paths can
// only be exercised against a real peer, so (like the tls tests requiring openssl)
// this is an accepted coverage-gate dependency. Fails loudly if the infra is missing.
void require_node() {
    static const bool has_node = [] {
        const std::string check = "command -v '" + std::string(NODE_EXECUTABLE) +
                                  "' >/dev/null 2>&1 && test -d '" +
                                  std::string(WEBSOCKET_FIXTURE_DIR) + "/node_modules/ws'";
        return std::system(check.c_str()) == 0;
    }();
    ASSERT_TRUE(has_node)
        << "node + the `ws` package are required test infrastructure (run: cd tests/fixtures "
           "&& npm install ws). Coverage runs require them.";
}

}  // namespace

// The full path: TLS 1.3 handshake + RFC 6455 upgrade + a text echo round trip +
// clean close, against the real Node `ws` server. This is the @systest anchor.
TEST(WebSocketSys, EchoRoundTrip) {
    require_node();
    WssEchoServer server(48951);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    EXPECT_EQ(ws::send_text(s, "hello"), 5);
    EXPECT_EQ(ws::recv(s), "hello");
    // A second round trip on the same session (exercises the reused read buffer at steady state).
    EXPECT_EQ(ws::send_text(s, "world"), 5);
    EXPECT_EQ(ws::recv(s), "world");
    EXPECT_EQ(ws::close(s), 0);
}

// The low-level connect(host, port, path, server_name) entry point (not the URL form),
// plus a medium message that forces the 16-bit extended length header (payload > 125).
TEST(WebSocketSys, ConnectAndExtendedLength16) {
    require_node();
    WssEchoServer server(48952);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect("localhost", server.port(), "/", "localhost", false, server.cert_path());
    ASSERT_GE(s, 0);
    const std::string msg(1000, 'x');  // > 125 and <= 0xFFFF -> 2-byte length field
    EXPECT_EQ(ws::send_text(s, msg), 1000);
    EXPECT_EQ(ws::recv(s), msg);
    EXPECT_EQ(ws::close(s), 0);
}

// A large message that forces the 64-bit extended length header (payload > 0xFFFF),
// exercising put_header's 8-byte length branch on send and recv's len==127 branch.
TEST(WebSocketSys, ExtendedLength64) {
    require_node();
    WssEchoServer server(48953);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    const std::string msg(70000, 'Z');  // > 0xFFFF -> 8-byte length field
    EXPECT_EQ(ws::send_text(s, msg), 70000);
    // ws MAY deliver a large echo as a single frame; recv reassembles regardless.
    std::string got = ws::recv(s);
    EXPECT_EQ(got, msg);
    EXPECT_EQ(ws::close(s), 0);
}

// The RAII Client guard round trip: open_url / send_text / recv / is_open / id, then
// move-construct, move-assign, shutdown and explicit close — mirrors tls's ConnGuard test.
TEST(WebSocketSys, ClientGuardRoundTrip) {
    require_node();
    WssEchoServer server(48954);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    ws::Client c = ws::open_url(server.url(), false, server.cert_path());
    ASSERT_TRUE(c.is_open());
    EXPECT_GT(c.id(), 0);
    EXPECT_EQ(c.send_text("guarded"), 7);
    EXPECT_EQ(c.recv(), "guarded");

    ws::Client active(std::move(c));  // move-construct: transfer ownership
    EXPECT_FALSE(c.is_open());
    EXPECT_TRUE(active.is_open());
    EXPECT_EQ(active.send_text("moved"), 5);
    EXPECT_EQ(active.recv(), "moved");

    ws::Client sink;                  // default-constructed: closed
    EXPECT_FALSE(sink.is_open());
    sink = std::move(active);         // move-assign onto a closed guard
    EXPECT_FALSE(active.is_open());
    EXPECT_TRUE(sink.is_open());

    EXPECT_EQ(sink.shutdown(), 0);    // half-close the socket (wake any reader)
    EXPECT_EQ(sink.close(), 0);
    EXPECT_EQ(sink.close(), -1);      // idempotent: already closed
}

// The RAII open(host, port, path, server_name) form (guarded low-level connect), and
// a move-assign that closes a still-OPEN session first (the operator=(&&) close branch).
TEST(WebSocketSys, ClientOpenAndMoveAssignClosesOpen) {
    require_node();
    WssEchoServer server(48955);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    ws::Client a = ws::open("localhost", server.port(), "/", "localhost", false, server.cert_path());
    ASSERT_TRUE(a.is_open());
    EXPECT_EQ(a.send_text("a"), 1);
    EXPECT_EQ(a.recv(), "a");

    ws::Client b = ws::open_url(server.url(), false, server.cert_path());  // a SECOND open session
    ASSERT_TRUE(b.is_open());
    b = std::move(a);  // b was open -> operator=(&&) must close b's old session first
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.send_text("still-alive"), 11);
    EXPECT_EQ(b.recv(), "still-alive");
    // b's destructor closes the surviving session here.
}

// The server-initiated clean close: sending "close" makes the ws server send a real
// RFC 6455 close frame. cheatah's recv sees opcode 0x8, echoes a close frame back, marks
// the session closed and returns "" (EOF). A SECOND recv returns "" via the s->closed
// short-circuit. Exercises recv's close-frame branch AND the closed-session fast path.
TEST(WebSocketSys, ServerCloseYieldsEmptyRecv) {
    require_node();
    WssEchoServer server(48956);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    EXPECT_EQ(ws::send_text(s, "ping-echo"), 9);
    EXPECT_EQ(ws::recv(s), "ping-echo");
    ws::send_text(s, "close");        // ask the server for a clean close handshake
    EXPECT_EQ(ws::recv(s), "");       // opcode 0x8 -> echo close, mark closed, return ""
    EXPECT_EQ(ws::recv(s), "");       // s->closed short-circuit (no I/O)
    EXPECT_EQ(ws::close(s), 0);       // close() after a peer close: !closed is false, just teardown
}

// The control-frame paths: a server-initiated ping (recv answers pong transparently),
// a server pong (ignored), then a real message. Exercises recv's opcode 0x9 and 0xA
// branches, which the caller never sees.
TEST(WebSocketSys, PingPongTransparent) {
    require_node();
    WssEchoServer server(48958);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    ws::send_text(s, "ping");             // server: ping, then pong, then "after-ping"
    EXPECT_EQ(ws::recv(s), "after-ping");  // ping->pong + pong-ignore handled internally
    EXPECT_EQ(ws::close(s), 0);
}

// The fragmentation/continuation path: the server sends a message as two frames (text
// fin=false + continuation fin=true) via ws's own framer; recv reassembles them into a
// single message. Exercises the opcode 0x0 continuation branch and the reassembly buffer.
TEST(WebSocketSys, ReassemblesFragmentedMessage) {
    require_node();
    WssEchoServer server(48959);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    ws::send_text(s, "frag");
    EXPECT_EQ(ws::recv(s), "frag-one|frag-two");
    EXPECT_EQ(ws::close(s), 0);
}

// The defensive server-masked-frame path: RFC 6455 §5.1 forbids a server from masking,
// but the cheatah client unmasks defensively anyway. We ask ws's OWN sender to mask a
// server->client frame (no frame bytes are hand-written in our tree) so recv's masked
// branch (mask key read + mask_into) is exercised against the real library's masker.
TEST(WebSocketSys, UnmasksMaskedServerFrame) {
    require_node();
    WssEchoServer server(48960);
    ASSERT_TRUE(server.ok()) << "could not start node ws echo server (test infrastructure)";

    const long long s = ws::connect_url(server.url(), false, server.cert_path());
    ASSERT_GE(s, 0);
    ws::send_text(s, "masked");
    EXPECT_EQ(ws::recv(s), "masked-ok");  // recv must unmask the (irregular) masked frame
    EXPECT_EQ(ws::close(s), 0);
}

// TLS handshake failure: connect to a peer that accepts TCP but is not a TLS server,
// so tls::client_connect fails -> connect() must throw and close the fd (142-144).
TEST(WebSocketSys, RefusesTlsHandshakeFailure) {
    const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(listen_fd, 0);
    const long long port = sock::local_port(listen_fd);
    std::thread peer([listen_fd]() {
        const long long client = sock::accept(listen_fd);
        if (client >= 0) {
            sock::sendall(client, "definitely not a TLS server\r\n");
            sock::close(client);
        }
    });
    EXPECT_THROW(ws::connect("127.0.0.1", port, "/", "localhost"), std::runtime_error);
    peer.join();
    sock::close(listen_fd);
}

// The upgrade-response error paths against a REAL TLS peer (Node's built-in tls) that
// completes the handshake but never sends a valid upgrade: "drop" closes the socket with
// no response (connection closed during upgrade, 165-167), "flood" writes >64 KiB with no
// blank line (upgrade response too large / not a WebSocket server, 171-174).
TEST(WebSocketSys, RefusesClosedDuringUpgrade) {
    require_node();
    WssEchoServer server(48961, "ec -pkeyopt ec_paramgen_curve:prime256v1", "drop");
    ASSERT_TRUE(server.ok()) << "could not start node tls drop server (test infrastructure)";
    EXPECT_THROW(ws::connect("localhost", server.port(), "/", "localhost", false, server.cert_path()), std::runtime_error);
}

TEST(WebSocketSys, RefusesOversizeUpgradeResponse) {
    require_node();
    WssEchoServer server(48962, "ec -pkeyopt ec_paramgen_curve:prime256v1", "flood");
    ASSERT_TRUE(server.ok()) << "could not start node tls flood server (test infrastructure)";
    EXPECT_THROW(ws::connect("localhost", server.port(), "/", "localhost", false, server.cert_path()), std::runtime_error);
}

// connect_url rejects a non-wss scheme, and accepts the no-port / no-path URL forms
// (default port 443, default path "/") — exercising connect_url's parsing branches
// even though the default-port connect then fails (no server on 443 here).
TEST(WebSocketSys, ConnectUrlParsingBranches) {
    // Non-wss scheme -> immediate throw (no network).
    EXPECT_THROW(ws::connect_url("ws://localhost/"), std::runtime_error);
    EXPECT_THROW(ws::connect_url("https://localhost/"), std::runtime_error);
    // wss with NO explicit port and NO path: default port 443, default path "/".
    // The connect then fails (nothing on 443), which is expected — the parsing
    // branches (colon==npos, slash==npos) run before the failing connect.
    EXPECT_THROW(ws::connect_url("wss://127.0.0.1"), std::runtime_error);
}

// The upgrade-refusal path: a peer that completes TLS but answers the HTTP upgrade
// with something OTHER than 101 must be rejected ("server did not switch protocols").
// A plain https server (no `ws`) returns a normal HTTP status, exercising that branch.
TEST(WebSocketSys, RefusesNon101) {
    require_node();
    // A plain HTTPS server (no WebSocketServer): completes TLS, but answers the upgrade
    // GET with HTTP 200 instead of 101 Switching Protocols.
    WssEchoServer server(48957, "ec -pkeyopt ec_paramgen_curve:prime256v1", "plain");
    ASSERT_TRUE(server.ok()) << "could not start node https server (test infrastructure)";
    // cheatah must reject: "server did not switch protocols".
    EXPECT_THROW(
        {
            const long long s = ws::connect("localhost", server.port(), "/", "localhost", false, server.cert_path());
            (void)s;
        },
        std::runtime_error);
}
