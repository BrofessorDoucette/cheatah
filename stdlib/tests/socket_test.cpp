#include "socket.hpp"

#include <string>
#include <thread>

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
