// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "socket.hpp"

#include <cstddef>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <gtest/gtest.h>

namespace sk = cheatah::socket;

// A real loopback round-trip: a listener accepts one client, reads its request,
// and replies; a client thread connects, sends, and reads the reply. Exercises
// tcp_listen / local_port / accept / recv / sendall / tcp_connect / send / close.
TEST(CheatahSocket, Loopback) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 4);
    ASSERT_GE(lfd, 0) << sk::last_error();
    const long long port = sk::local_port(lfd);
    ASSERT_GT(port, 0);

    std::string client_reply;
    std::thread client([&] {
        const long long cfd = sk::tcp_connect("127.0.0.1", port);
        ASSERT_GE(cfd, 0);
        ASSERT_EQ(sk::sendall(cfd, "ping"), 0);
        client_reply = sk::recv(cfd, 64);
        sk::close(cfd);
    });

    const long long conn = sk::accept(lfd);
    ASSERT_GE(conn, 0) << sk::last_error();
    EXPECT_EQ(sk::recv(conn, 64), "ping");
    EXPECT_EQ(sk::sendall(conn, "pong"), 0);
    sk::close(conn);

    client.join();
    EXPECT_EQ(client_reply, "pong");
    sk::close(lfd);
}

// A larger, binary-ish payload to drive send()/sendall() through multiple writes
// and confirm byte-exactness (including an embedded NUL).
TEST(CheatahSocket, Sendall) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(lfd, 0);
    const long long port = sk::local_port(lfd);

    std::string payload(8000, 'x');
    payload[100] = '\0';
    payload[7999] = 'Z';

    std::thread client([&] {
        const long long cfd = sk::tcp_connect("127.0.0.1", port);
        ASSERT_GE(cfd, 0);
        EXPECT_GT(sk::send(cfd, "hi"), 0);   // single send()
        EXPECT_EQ(sk::sendall(cfd, payload), 0);
        sk::close(cfd);
    });

    const long long conn = sk::accept(lfd);
    ASSERT_GE(conn, 0);
    std::string got;
    while (got.size() < payload.size() + 2) {
        const std::string chunk = sk::recv(conn, 4096);
        if (chunk.empty()) break;
        got += chunk;
    }
    EXPECT_EQ(got, "hi" + payload);
    sk::close(conn);
    client.join();
    sk::close(lfd);
}

#if !defined(_WIN32)
// THE "it's the default for every user" guarantee: every connected stream socket — client AND
// accepted server side — comes tuned for throughput (TCP_NODELAY on, an enlarged SO_RCVBUF)
// WITHOUT any caller opt-in. This is the regression guard that a future refactor can't silently
// drop the tuning that turns a 0.2 MB/s crawl into a full-speed download.
TEST(CheatahSocket, ConnectedSocketsAreTunedByDefault) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(lfd, 0) << sk::last_error();
    const long long port = sk::local_port(lfd);

    long long cfd = -1;
    std::thread client([&] { cfd = sk::tcp_connect("127.0.0.1", port); });
    const long long conn = sk::accept(lfd);
    client.join();
    ASSERT_GE(cfd, 0) << sk::last_error();
    ASSERT_GE(conn, 0) << sk::last_error();

    const auto nodelay = [](long long fd) {
        int v = 0;
        socklen_t len = sizeof(v);
        EXPECT_EQ(::getsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_NODELAY, &v, &len), 0);
        return v;
    };
    const auto rcvbuf = [](long long fd) {
        int v = 0;
        socklen_t len = sizeof(v);
        EXPECT_EQ(::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVBUF, &v, &len), 0);
        return v;  // Linux reports 2× the requested value
    };
    // Nagle is OFF on both ends.
    EXPECT_NE(nodelay(cfd), 0) << "client socket left Nagle on";
    EXPECT_NE(nodelay(conn), 0) << "accepted socket left Nagle on";
    // The receive buffer is well above the kernel's stock default (131072 here) — proof the
    // window was opened. Floor chosen below the requested 4 MiB to tolerate rmem_max clamping.
    EXPECT_GE(rcvbuf(cfd), 512 * 1024) << "client SO_RCVBUF not enlarged";
    EXPECT_GE(rcvbuf(conn), 512 * 1024) << "accepted SO_RCVBUF not enlarged";

    sk::close(cfd);
    sk::close(conn);
    sk::close(lfd);
}

// A multi-megabyte loopback transfer read back byte-for-byte, and fast — the socket-layer
// throughput regression guard. If a change reverted the tuning or crippled recv, a several-MB
// loopback transfer would still be correct but this asserts it also stays quick.
TEST(CheatahSocket, BulkTransferIsCorrectAndPrompt) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(lfd, 0);
    const long long port = sk::local_port(lfd);

    // 8 MiB with a position-dependent pattern so any misorder/truncation is caught.
    std::string payload(std::size_t{8} * 1024 * 1024, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>((i * 1103515245u + 12345u) >> 16);
    }

    std::thread server([&] {
        const long long conn = sk::accept(lfd);
        ASSERT_GE(conn, 0);
        EXPECT_EQ(sk::sendall(conn, payload), 0);
        sk::close(conn);
    });

    const auto t0 = std::chrono::steady_clock::now();
    const long long cfd = sk::tcp_connect("127.0.0.1", port);
    ASSERT_GE(cfd, 0);
    std::string got;
    got.reserve(payload.size());
    while (got.size() < payload.size()) {
        const std::string chunk = sk::recv(cfd, 65536);
        if (chunk.empty()) break;
        got += chunk;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sk::close(cfd);
    server.join();
    sk::close(lfd);

    ASSERT_EQ(got.size(), payload.size());
    EXPECT_EQ(got, payload);
    // Loopback moves GB/s; 8 MiB in >5 s would mean the recv path regressed hard. Generous so CI
    // load never flakes it.
    EXPECT_LT(secs, 5.0) << "8 MiB loopback took " << secs << "s — recv path regressed";
}
#endif  // !_WIN32

// The low-level BSD path: socket -> set_reuseaddr -> bind -> listen -> local_port.
TEST(CheatahSocket, ListenLowLevel) {
    const long long fd = sk::socket();
    ASSERT_GE(fd, 0);
    EXPECT_EQ(sk::set_reuseaddr(fd), 0);
    EXPECT_EQ(sk::bind(fd, "127.0.0.1", 0), 0);
    EXPECT_EQ(sk::listen(fd, 1), 0);
    EXPECT_GT(sk::local_port(fd), 0);
    EXPECT_EQ(sk::close(fd), 0);
}

// Connecting to a port nobody is listening on fails, and last_error() is set.
// A datagram round trip on loopback: a bound receiver, an unbound sender, the sender's address
// reported back, a receive window that expires empty, and a packet larger than the buffer truncated
// — every property the datagram contract promises, none of the stream's.
TEST(CheatahSocket, UdpLoopback) {
    const long long rx = sk::udp_socket();
    ASSERT_GE(rx, 0) << sk::last_error();
    ASSERT_EQ(sk::bind(rx, "127.0.0.1", 0), 0) << sk::last_error();
    const long long port = sk::local_port(rx);
    ASSERT_GT(port, 0);
    ASSERT_EQ(sk::set_timeout(rx, 500), 0);
    const long long tx = sk::udp_socket();
    ASSERT_GE(tx, 0);
    EXPECT_EQ(sk::sendto(tx, "127.0.0.1", port, "presence 1"), 10);
    std::string from;
    long long from_port = 0;
    EXPECT_EQ(sk::recvfrom(rx, 64, from, from_port), "presence 1");
    EXPECT_EQ(from, "127.0.0.1");
    EXPECT_GT(from_port, 0);
    // Nothing sent: the window passes and the receive is empty, not an error.
    EXPECT_EQ(sk::recvfrom(rx, 64, from, from_port), "");
    EXPECT_EQ(from_port, 0);
    // A packet larger than the buffer is truncated to it — the datagram contract, stated in the doc.
    EXPECT_EQ(sk::sendto(tx, "127.0.0.1", port, std::string(100, 'x')), 100);
    EXPECT_EQ(sk::recvfrom(rx, 16, from, from_port).size(), 16u);
    EXPECT_EQ(sk::close(tx), 0);
    EXPECT_EQ(sk::close(rx), 0);
}

TEST(CheatahSocket, ConnectRefused) {
    // Bind+listen to grab a free port, then close it so the port is free again.
    const long long probe = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(probe, 0);
    const long long dead_port = sk::local_port(probe);
    sk::close(probe);

    EXPECT_LT(sk::tcp_connect("127.0.0.1", dead_port), 0);
    EXPECT_FALSE(sk::last_error().empty());
}

// Name resolution failure is reported as -1, not a crash.
TEST(CheatahSocket, ResolveFailure) {
    const long long fd = sk::socket();
    ASSERT_GE(fd, 0);
    EXPECT_LT(sk::bind(fd, "no.such.host.invalid.", 0), 0);
    EXPECT_LT(sk::tcp_connect("no.such.host.invalid.", 80), 0);
    EXPECT_LT(sk::tcp_listen("no.such.host.invalid.", 0, 1), 0);  // exercises tcp_listen's error path
    sk::close(fd);
}

// Operations on a bad fd return errors (and recv returns ""), never crash.
TEST(CheatahSocket, BadFd) {
    EXPECT_EQ(sk::recv(-1, 64), "");
    EXPECT_EQ(sk::recv(5, 0), "");          // non-positive bufsize
    EXPECT_LT(sk::send(-1, "x"), 0);
    EXPECT_LT(sk::sendall(-1, "x"), 0);
    EXPECT_LT(sk::listen(-1, 1), 0);
    EXPECT_LT(sk::accept(-1), 0);
    EXPECT_LT(sk::local_port(-1), 0);
    EXPECT_LT(sk::connect(-1, "127.0.0.1", 80), 0);
    EXPECT_LT(sk::close(-1), 0);
}

// set_timeout (recv/send deadline) + shutdown (graceful SHUT_RDWR) on a live connection.
// A loopback pair so both run on a real connected socket; shutdown wakes the peer's recv.
TEST(CheatahSocket, TimeoutThenShutdown) {
    const long long lfd = sk::tcp_listen("127.0.0.1", 0, 1);
    ASSERT_GE(lfd, 0) << sk::last_error();
    const long long port = sk::local_port(lfd);
    ASSERT_GT(port, 0);
    std::thread client([&] {
        const long long c = sk::tcp_connect("127.0.0.1", port);
        if (c >= 0) { sk::recv(c, 1); sk::close(c); }
    });
    const long long conn = sk::accept(lfd);
    ASSERT_GE(conn, 0) << sk::last_error();
    EXPECT_EQ(sk::set_timeout(conn, 500), 0);   // positive timeout -> SO_RCVTIMEO/SO_SNDTIMEO
    EXPECT_EQ(sk::shutdown(conn), 0);           // graceful SHUT_RDWR
    sk::close(conn);
    client.join();
    sk::close(lfd);
}

// ---- owning RAII guards (Conn / Listener): the leak-proof, `with`-friendly API ----

// A default-constructed Conn owns nothing: closed, fd == -1, and close() reports -1.
TEST(CheatahSocket, ConnDefaultIsClosed) {
    sk::Conn c;
    EXPECT_FALSE(c.is_open());
    EXPECT_EQ(c.fd(), -1);
    EXPECT_EQ(c.close(), -1);  // nothing to close
    // A failed open() also yields a closed Conn (no fd leaked on the error path).
    sk::Listener probe = sk::serve("127.0.0.1", 0, 1);
    const long long dead = probe.local_port();
    probe.close();
    sk::Conn bad = sk::open("127.0.0.1", dead);
    EXPECT_FALSE(bad.is_open());
}

// Likewise for a default-constructed Listener.
TEST(CheatahSocket, ListenerDefaultIsClosed) {
    sk::Listener l;
    EXPECT_FALSE(l.is_open());
    EXPECT_EQ(l.fd(), -1);
    EXPECT_EQ(l.close(), -1);
}

// Explicit close() releases the fd and is idempotent; the destructor then does nothing.
TEST(CheatahSocket, ConnGuardClosesOnScopeExit) {
    sk::Listener server = sk::serve("127.0.0.1", 0, 1);
    ASSERT_TRUE(server.is_open()) << sk::last_error();
    sk::Conn c = sk::open("127.0.0.1", server.local_port());
    ASSERT_TRUE(c.is_open()) << sk::last_error();
    EXPECT_GE(c.fd(), 0);
    EXPECT_EQ(c.close(), 0);   // explicit close
    EXPECT_FALSE(c.is_open());
    EXPECT_EQ(c.close(), -1);  // idempotent
    // `server` is closed by its destructor here — Valgrind confirms no fd leak.
}

// Move transfers fd ownership; the moved-from guard is left closed (never double-closed).
TEST(CheatahSocket, ConnMoveTransfersOwnership) {
    sk::Listener server = sk::serve("127.0.0.1", 0, 2);
    ASSERT_TRUE(server.is_open());
    sk::Conn a = sk::open("127.0.0.1", server.local_port());
    ASSERT_TRUE(a.is_open());
    const long long fd = a.fd();
    sk::Conn b(std::move(a));   // move-construct
    EXPECT_FALSE(a.is_open());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is the assertion
    EXPECT_EQ(b.fd(), fd);
    // Move-assign onto an already-open guard closes the overwritten fd first.
    sk::Conn d = sk::open("127.0.0.1", server.local_port());
    ASSERT_TRUE(d.is_open());
    d = std::move(b);
    EXPECT_FALSE(b.is_open());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is the assertion
    EXPECT_EQ(d.fd(), fd);
    EXPECT_EQ(d.close(), 0);
}

// Full loopback through the guards: serve()/open()/accept() + Conn send/sendall/recv/
// set_timeout/local_port/shutdown/close — the whole owning API on a live connection.
TEST(CheatahSocket, ConnLoopback) {
    sk::Listener server = sk::serve("127.0.0.1", 0, 4);
    ASSERT_TRUE(server.is_open()) << sk::last_error();
    EXPECT_GE(server.fd(), 0);
    const long long port = server.local_port();
    ASSERT_GT(port, 0);

    std::string client_reply;
    std::thread client([&] {
        sk::Conn c = sk::open("127.0.0.1", port);
        ASSERT_TRUE(c.is_open());
        EXPECT_EQ(c.set_timeout(1000), 0);
        EXPECT_GT(c.local_port(), 0);
        EXPECT_GT(c.send("pi"), 0);      // single send()
        EXPECT_EQ(c.sendall("ng"), 0);   // looped sendall()
        client_reply = c.recv(64);
        // c is closed by its destructor at thread-scope exit.
    });

    sk::Conn conn = server.accept();
    ASSERT_TRUE(conn.is_open()) << sk::last_error();
    std::string got;
    while (got.size() < 4) {
        const std::string chunk = conn.recv(64);
        if (chunk.empty()) break;
        got += chunk;
    }
    EXPECT_EQ(got, "ping");
    EXPECT_EQ(conn.sendall("pong"), 0);
    EXPECT_EQ(conn.shutdown(), 0);       // graceful half-close
    EXPECT_EQ(conn.close(), 0);

    client.join();
    EXPECT_EQ(client_reply, "pong");
    // `server` is closed by its destructor.
}

// Listener move semantics + idempotent close.
TEST(CheatahSocket, ListenerLoopback) {
    sk::Listener a = sk::serve("127.0.0.1", 0, 1);
    ASSERT_TRUE(a.is_open()) << sk::last_error();
    const long long port = a.local_port();
    ASSERT_GT(port, 0);
    sk::Listener b(std::move(a));  // move-construct
    EXPECT_FALSE(a.is_open());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is the assertion
    EXPECT_EQ(b.local_port(), port);
    sk::Listener c;
    c = std::move(b);              // move-assign onto a closed listener
    EXPECT_FALSE(b.is_open());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move): moved-from state is the assertion
    EXPECT_GE(c.fd(), 0);
    EXPECT_EQ(c.close(), 0);
    EXPECT_EQ(c.close(), -1);      // idempotent
}
