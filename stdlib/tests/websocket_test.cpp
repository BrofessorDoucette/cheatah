// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// websocket_test — offline unit checks for the websocket client. The framing
// and live round-trip are covered by the system tests (a real WSS server);
// here we cover the input validation that needs no network.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "socket.hpp"
#include "websocket.hpp"
#include "websocket_lowlevel.hpp"  // recv()/close() + the CHEATAH_WEBSOCKET_TESTING frame-parser seam

namespace ws = cheatah::websocket;
namespace sk = cheatah::socket;

namespace {

// Build a raw (unmasked, server-style) WebSocket frame: FIN|opcode, a length encoding, payload.
std::string frame(unsigned char b0, const std::string& payload) {
    std::string f(1, static_cast<char>(b0));
    const std::uint64_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<char>(n));
    } else {
        f.push_back(static_cast<char>(126));
        f.push_back(static_cast<char>((n >> 8) & 0xFF));
        f.push_back(static_cast<char>(n & 0xFF));
    }
    return f + payload;
}

// A frame HEADER that DECLARES a length via the 64-bit field but carries no payload — the shape a
// malicious server uses to overflow `header + len`. The cap must reject it before any read/copy.
std::string len64_header(unsigned char b0, std::uint64_t declared) {
    std::string f(1, static_cast<char>(b0));
    f.push_back(static_cast<char>(127));
    for (int sh = 56; sh >= 0; sh -= 8) f.push_back(static_cast<char>((declared >> sh) & 0xFF));
    return f;
}

// A control-frame header declaring a length via the 16-bit field, no payload.
std::string len16_header(unsigned char b0, std::uint64_t declared) {
    std::string f(1, static_cast<char>(b0));
    f.push_back(static_cast<char>(126));
    f.push_back(static_cast<char>((declared >> 8) & 0xFF));
    f.push_back(static_cast<char>(declared & 0xFF));
    return f;
}

// Drive recv() over a synthetic session pre-loaded with `bytes` (frame-parser fuzzing seam), then
// free it. Returns recv()'s result; rethrows whatever recv throws (after freeing the session).
std::string recv_bytes(const std::string& bytes, std::uint64_t max_frame = 0,
                       std::uint64_t max_message = 0) {
    const long long h = ws::testonly::session_from_bytes(bytes, max_frame, max_message);
    std::string out;
    try {
        out = ws::recv(h);
    } catch (...) {
        ws::close(h);
        throw;
    }
    ws::close(h);
    return out;
}

}  // namespace

// === Red-team: a malicious server must not corrupt memory, OOM, or bypass RFC 6455 framing. ===

// F1 (CRITICAL): a 64-bit length near 2^64 must be rejected BEFORE it overflows `header + len`
// and drives an out-of-bounds unmask/copy. This is the memory-corruption bug.
TEST(CheatahWebSocket, RejectsOverflowingFrameLength) {
    // opcode 0x2 (binary), FIN; declared length = 0xFFFFFFFFFFFFFFFF (would wrap header+len).
    EXPECT_THROW(recv_bytes(len64_header(0x82, 0xFFFFFFFFFFFFFFFFull)), std::runtime_error);
    // A merely-huge (non-wrapping) length is rejected the same way (would otherwise OOM).
    EXPECT_THROW(recv_bytes(len64_header(0x82, 8ull << 30)), std::runtime_error);  // 8 GiB
    // Even the MASKED path (the actual out-of-bounds-write trigger) is refused before unmasking.
    std::string masked = len64_header(0x82, 1ull << 40);
    masked[1] = static_cast<char>(0x80 | 127);  // set the MASK bit in b1
    EXPECT_THROW(recv_bytes(masked), std::runtime_error);
}

// F1: a frame just over the (here-tiny) cap is rejected; one at the cap is accepted.
TEST(CheatahWebSocket, FramePayloadCapEnforced) {
    EXPECT_THROW(recv_bytes(frame(0x82, std::string(101, 'x')), /*max_frame=*/100), std::runtime_error);
    EXPECT_EQ(recv_bytes(frame(0x82, std::string(100, 'x')), /*max_frame=*/100), std::string(100, 'x'));
}

// F2 (OOM): a fragmented message that would exceed the reassembly cap is rejected.
TEST(CheatahWebSocket, ReassembledMessageCapEnforced) {
    // First fragment (text, FIN=0) of 5 bytes, then a continuation of 5 more; cap = 8 < 10.
    const std::string frames = frame(0x01, "aaaaa") + frame(0x80, "bbbbb");
    EXPECT_THROW(recv_bytes(frames, /*max_frame=*/0, /*max_message=*/8), std::runtime_error);
}

// F3: control frames must be <=125 bytes and MUST NOT be fragmented (RFC 6455 §5.5).
TEST(CheatahWebSocket, RejectsOversizedControlFrame) {
    EXPECT_THROW(recv_bytes(len16_header(0x89, 200)), std::runtime_error);  // ping, 200 bytes
}
TEST(CheatahWebSocket, RejectsFragmentedControlFrame) {
    EXPECT_THROW(recv_bytes(frame(0x09, "abc")), std::runtime_error);  // ping, FIN=0
}

// F4: reserved bits set (no extension negotiated) and undefined opcodes fail the connection.
TEST(CheatahWebSocket, RejectsReservedBits) {
    EXPECT_THROW(recv_bytes(frame(0xC1, "")), std::runtime_error);  // RSV1 | text | FIN
}
TEST(CheatahWebSocket, RejectsUnknownOpcode) {
    EXPECT_THROW(recv_bytes(frame(0x83, "")), std::runtime_error);  // opcode 0x3 (undefined)
}

// Fragmentation state machine: a stray continuation, or a new data frame mid-message, is invalid.
TEST(CheatahWebSocket, RejectsContinuationWithNoMessage) {
    EXPECT_THROW(recv_bytes(frame(0x80, "x")), std::runtime_error);  // continuation, FIN, no msg
}
TEST(CheatahWebSocket, RejectsNewDataFrameDuringFragment) {
    const std::string frames = frame(0x01, "ab") + frame(0x81, "cd");  // text(FIN=0) then text(FIN)
    EXPECT_THROW(recv_bytes(frames), std::runtime_error);
}

// === Blue-team: the valid paths still work (single frame, fragmentation, an interleaved pong). ===
TEST(CheatahWebSocket, AcceptsValidSingleFrame) {
    EXPECT_EQ(recv_bytes(frame(0x81, "hello")), "hello");   // text, FIN
    const std::string bin("\x00\x01\x02", 3);               // NUL-containing binary payload
    EXPECT_EQ(recv_bytes(frame(0x82, bin)), bin);           // binary, FIN — byte-safe
}
TEST(CheatahWebSocket, AcceptsFragmentedMessage) {
    const std::string frames = frame(0x01, "he") + frame(0x00, "l") + frame(0x80, "lo");
    EXPECT_EQ(recv_bytes(frames), "hello");
}
TEST(CheatahWebSocket, SkipsPongThenReturnsData) {
    const std::string frames = frame(0x8A, "") + frame(0x81, "ok");  // pong (ignored), then text
    EXPECT_EQ(recv_bytes(frames), "ok");
}

TEST(CheatahWebSocket, ConnectUrlRejectsNonWss) {
    // Only wss:// is supported; a non-wss scheme fails fast, before any socket work. open_url()
    // is the cheatah-facing guard factory (it delegates to the C++-only connect_url()).
    EXPECT_THROW(ws::open_url("ws://example.com/"), std::runtime_error);
    EXPECT_THROW(ws::open_url("https://example.com/"), std::runtime_error);
    EXPECT_THROW(ws::open_url("example.com"), std::runtime_error);
}

// A default-constructed Client owns nothing: closed, id() == 0, and close() reports -1 without
// touching a session. open_url() on a non-wss scheme throws before a session is ever created,
// so the guard is never left holding a bogus handle. (Functional send/recv/close over a live
// session are covered by WebSocketSys.EchoRoundTrip against a real wss peer.)
TEST(CheatahWebSocket, ClientDefaultIsClosed) {
    ws::Client c;
    EXPECT_FALSE(c.is_open());
    EXPECT_EQ(c.id(), 0);
    EXPECT_EQ(c.close(), -1);  // nothing to close
    EXPECT_THROW(ws::open_url("ws://example.com/"), std::runtime_error);
}

// ---- plaintext ws:// -------------------------------------------------------------------
// The client is TLS-only by default and gained a PLAINTEXT mode for one reason: Chrome's
// DevTools endpoint speaks ws:// on loopback and offers no TLS at all. These pin the two
// properties that keep "insecure" from becoming reachable by accident.

// The guard. Plaintext to anything that is not this machine is refused before a socket is
// even opened, so no configuration turns this into a cleartext WebSocket to the internet.
TEST(WebSocketPlaintext, RefusesNonLoopbackHost) {
    try {
        ws::connect(std::string("example.com"), 80, std::string("/"), std::string("example.com"),
                    false, std::string(""), /*secure=*/false);
        FAIL() << "plaintext to a non-loopback host must be refused";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("loopback"), std::string::npos) << msg;
        EXPECT_NE(msg.find("wss://"), std::string::npos) << msg;  // says what to use instead
    }
}

TEST(WebSocketPlaintext, LoopbackSpellingsAreAllAccepted) {
    // Refused for a reason OTHER than the loopback guard: nothing is listening, so this gets
    // as far as the TCP connect. That is the point — the guard let it through.
    for (const char* host : {"127.0.0.1", "::1", "localhost"}) {
        try {
            ws::connect(std::string(host), 1, std::string("/"), std::string(host), false,
                        std::string(""), /*secure=*/false);
            FAIL() << "port 1 should not have accepted a connection";
        } catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            EXPECT_EQ(msg.find("loopback"), std::string::npos)
                << host << " was refused by the loopback guard: " << msg;
        }
    }
}

// The plaintext path end to end as far as it can go without a WebSocket server: a real TCP
// peer on loopback that accepts and closes. This is what exercises the skip-TLS branch and
// the upgrade exchange over socket:: rather than tls::.
TEST(WebSocketPlaintext, ConnectsOverPlainTcpAndReportsTheUpgradeFailure) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(lfd, 0);
    const long long port = sk::local_port(lfd);
    ASSERT_GT(port, 0);

    // Accept, read whatever arrives, then close without answering the upgrade.
    std::thread server([&] {
        const long long conn = sk::accept(lfd);
        if (conn >= 0) {
            sk::recv(conn, 4096);
            sk::close(conn);
        }
    });

    std::string what;
    try {
        ws::connect(std::string("127.0.0.1"), port, std::string("/"), std::string("127.0.0.1"),
                    false, std::string(""), /*secure=*/false);
        ADD_FAILURE() << "a peer that never answers the upgrade must not yield a session";
    } catch (const std::runtime_error& e) {
        what = e.what();
    }
    server.join();
    sk::close(lfd);

    // Either the request could not be sent or the peer closed mid-upgrade; both are the
    // plaintext transport reporting a real failure rather than a TLS one.
    EXPECT_FALSE(what.empty());
    EXPECT_EQ(what.find("TLS"), std::string::npos) << "plaintext must not report a TLS error: " << what;
}

// The upgrade-request SEND failure. connect() cannot reach this without racing a peer reset,
// so it is driven through the white-box seam: an invalid descriptor makes socket::send fail
// with EBADF every time. Pins that the failure is reported as an upgrade failure (not a TLS
// one) and that the session is destroyed on the way out rather than leaked.
TEST(WebSocketPlaintext, UpgradeSendFailureIsReported) {
    try {
        ws::testonly::send_upgrade_on_closed_fd();
        FAIL() << "sending an upgrade on a closed descriptor must throw";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("upgrade request failed"), std::string::npos) << msg;
        EXPECT_EQ(msg.find("TLS"), std::string::npos)
            << "a plaintext session must not report a TLS error: " << msg;
    }
}

// The plaintext (ws://) transport had two defects, both invisible to the wss:// tests:
//   * shutdown() returned -1 for ANY plaintext session — its own socket::shutdown branch was
//     unreachable behind a `tls < 0` guard that had already returned — so a reader blocked in
//     recv() could never be woken, and a valid session reported an error.
//   * close() skipped the RFC 6455 close frame unless the session was TLS.
// Both are exercised here over a real loopback socket.
TEST(CheatahWebSocket, PlaintextShutdownAndCloseUseTheSocket) {
    namespace sock = cheatah::socket;
    const long long listen_fd = sock::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(listen_fd, 0);
    const long long port = sock::local_port(listen_fd);

    long long peer = -1;
    std::thread accepter([&] { peer = sock::accept(listen_fd); });
    const long long client = sock::tcp_connect("127.0.0.1", port);
    accepter.join();
    ASSERT_GE(client, 0);
    ASSERT_GE(peer, 0);

    // shutdown() must reach socket::shutdown and SUCCEED — it used to return -1 unconditionally.
    const long long s = ws::testonly::plaintext_session_on_fd(client);
    EXPECT_EQ(ws::shutdown(s), 0) << "a plaintext session must be wakeable";
    EXPECT_EQ(ws::close(s), 0);

    // close() on a live plaintext session writes the close frame (opcode 0x8, client-masked,
    // so the 2-byte header carries the mask bit and a zero-length payload).
    long long peer2 = -1;
    std::thread accepter2([&] { peer2 = sock::accept(listen_fd); });
    const long long client2 = sock::tcp_connect("127.0.0.1", port);
    accepter2.join();
    ASSERT_GE(client2, 0);
    ASSERT_GE(peer2, 0);
    const long long s2 = ws::testonly::plaintext_session_on_fd(client2);
    EXPECT_EQ(ws::close(s2), 0);
    const std::string frame = sock::recv(peer2, 8);
    ASSERT_GE(frame.size(), 2U) << "close() sent no frame over the plaintext transport";
    EXPECT_EQ(static_cast<unsigned char>(frame[0]) & 0x0FU, 0x08U) << "not a close frame";

    sock::close(peer);
    sock::close(peer2);
    sock::close(listen_fd);
}
