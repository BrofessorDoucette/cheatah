// Compile-run unit tests for the `socket` module: one test per function. Each
// writes a tiny .purr that calls a single socket function, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/socket_test.cpp) and the
// per-module system-level test (StdlibE2E.Socket).
//
// Real networking has no fixed output, so each program asserts DETERMINISTIC
// PROPERTIES as booleans (io.print emits "True"/"False"): bind to 127.0.0.1
// port 0 (OS-assigned) so nothing depends on a fixed port or external network,
// connect to port 1 (nothing listens there) for the refusal paths, and use a
// bad fd (-1) for the error paths. Functions that block waiting on a peer are
// exercised only through their non-blocking bad-fd error path.
#include "e2e_harness.hpp"

// tcp_listen: binding a listener on 127.0.0.1:0 yields a valid (>= 0) fd.
TEST(SocketCompileRun, TcpListen) {
    e2e::expect_e2e("socket_tcp_listen", R"PURR(import io
import socket
let fd = socket.tcp_listen("127.0.0.1", 0, 1)
io.print(fd >= 0)
socket.close(fd)
)PURR", "True\n");
}

// local_port: after binding to port 0 the OS assigns a real (> 0) port.
TEST(SocketCompileRun, LocalPort) {
    e2e::expect_e2e("socket_local_port", R"PURR(import io
import socket
let fd = socket.tcp_listen("127.0.0.1", 0, 1)
io.print(socket.local_port(fd) > 0)
socket.close(fd)
)PURR", "True\n");
}

// tcp_connect: connecting to port 1 (nothing listening) is refused -> < 0.
TEST(SocketCompileRun, TcpConnect) {
    e2e::expect_e2e("socket_tcp_connect", R"PURR(import io
import socket
io.print(socket.tcp_connect("127.0.0.1", 1) < 0)
)PURR", "True\n");
}

// close: closing a live fd returns 0; closing a bad fd returns < 0.
TEST(SocketCompileRun, Close) {
    e2e::expect_e2e("socket_close", R"PURR(import io
import socket
let fd = socket.tcp_listen("127.0.0.1", 0, 1)
io.print(socket.close(fd) == 0, socket.close(-1) < 0)
)PURR", "True True\n");
}

// socket: creating a raw IPv4 TCP socket yields a valid (>= 0) fd.
TEST(SocketCompileRun, Socket) {
    e2e::expect_e2e("socket_socket", R"PURR(import io
import socket
let fd = socket.socket()
io.print(fd >= 0)
socket.close(fd)
)PURR", "True\n");
}

// set_reuseaddr: enabling SO_REUSEADDR on a fresh socket succeeds (returns 0).
TEST(SocketCompileRun, SetReuseaddr) {
    e2e::expect_e2e("socket_set_reuseaddr", R"PURR(import io
import socket
let fd = socket.socket()
io.print(socket.set_reuseaddr(fd) == 0)
socket.close(fd)
)PURR", "True\n");
}

// bind: binding a socket to 127.0.0.1:0 succeeds (returns 0).
TEST(SocketCompileRun, Bind) {
    e2e::expect_e2e("socket_bind", R"PURR(import io
import socket
let fd = socket.socket()
io.print(socket.bind(fd, "127.0.0.1", 0) == 0)
socket.close(fd)
)PURR", "True\n");
}

// listen: marking a bound socket passive returns 0; listen on a bad fd < 0.
TEST(SocketCompileRun, Listen) {
    e2e::expect_e2e("socket_listen", R"PURR(import io
import socket
let fd = socket.socket()
socket.bind(fd, "127.0.0.1", 0)
io.print(socket.listen(fd, 1) == 0, socket.listen(-1, 1) < 0)
socket.close(fd)
)PURR", "True True\n");
}

// connect: connecting to port 1 (nothing listening) is refused -> < 0.
TEST(SocketCompileRun, Connect) {
    e2e::expect_e2e("socket_connect", R"PURR(import io
import socket
let fd = socket.socket()
io.print(socket.connect(fd, "127.0.0.1", 1) < 0)
socket.close(fd)
)PURR", "True\n");
}

// last_error: after a failed call, last_error() reports a non-empty message.
TEST(SocketCompileRun, LastError) {
    e2e::expect_e2e("socket_last_error", R"PURR(import io
import socket
let r = socket.connect(-1, "127.0.0.1", 1)
io.print(r < 0, socket.last_error() != "")
)PURR", "True True\n");
}

// send: a single send() on a bad fd returns < 0 (error path).
TEST(SocketCompileRun, Send) {
    e2e::expect_e2e("socket_send", R"PURR(import io
import socket
io.print(socket.send(-1, "x") < 0)
)PURR", "True\n");
}

// sendall: looping send on a bad fd returns < 0 (error path).
TEST(SocketCompileRun, Sendall) {
    e2e::expect_e2e("socket_sendall", R"PURR(import io
import socket
io.print(socket.sendall(-1, "x") < 0)
)PURR", "True\n");
}

// recv: a real recv() blocks until a peer sends, so it can't be made
// deterministic standalone. Exercise only the non-blocking error paths: a bad
// fd and a non-positive bufsize both return "" without touching the socket.
TEST(SocketCompileRun, Recv) {
    e2e::expect_e2e("socket_recv", R"PURR(import io
import socket
io.print(socket.recv(-1, 64) == "", socket.recv(5, 0) == "")
)PURR", "True True\n");
}

// accept: a real accept() blocks until a client connects, so it can't be made
// deterministic standalone. Exercise only the non-blocking error path: accept
// on a bad fd returns < 0 without blocking.
TEST(SocketCompileRun, Accept) {
    e2e::expect_e2e("socket_accept", R"PURR(import io
import socket
io.print(socket.accept(-1) < 0)
)PURR", "True\n");
}
