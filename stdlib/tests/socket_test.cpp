#include "socket.hpp"

#include <string>
#include <thread>
#include <utility>

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
    EXPECT_FALSE(a.is_open());
    EXPECT_EQ(b.fd(), fd);
    // Move-assign onto an already-open guard closes the overwritten fd first.
    sk::Conn d = sk::open("127.0.0.1", server.local_port());
    ASSERT_TRUE(d.is_open());
    d = std::move(b);
    EXPECT_FALSE(b.is_open());
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
    EXPECT_FALSE(a.is_open());
    EXPECT_EQ(b.local_port(), port);
    sk::Listener c;
    c = std::move(b);              // move-assign onto a closed listener
    EXPECT_FALSE(b.is_open());
    EXPECT_GE(c.fd(), 0);
    EXPECT_EQ(c.close(), 0);
    EXPECT_EQ(c.close(), -1);      // idempotent
}
