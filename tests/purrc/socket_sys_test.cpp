// System-level test for the `socket` stdlib module: ONE cohesive program that
// exercises EVERY purr-callable function in stdlib/socket/socket.hpp end to end
// (purrc + the C++ backend + the runtime + the linked libcheatah_socket).
//
// Unlike the per-function compile-run probes (socket_cr_test.cpp), this stands up
// real loopback TCP connections on 127.0.0.1 and does full round-trips inside a
// SINGLE cheatah process, driving both the low-level BSD path and the high-level
// convenience helpers:
//
//   low-level :  socket -> set_reuseaddr -> bind -> listen -> local_port
//                          -> (client) socket -> connect -> accept
//                          -> send / recv / sendall / recv -> close
//   high-level:  tcp_listen -> local_port -> tcp_connect -> accept
//                          -> sendall -> recv
//   error path:  connect(-1, ...) deliberately fails -> last_error() is non-empty
//
// All 14 exported functions are called: tcp_listen, tcp_connect, accept, recv,
// send, sendall, close, socket, set_reuseaddr, bind, listen, connect,
// local_port, last_error.
//
// Why it doesn't deadlock in one process: for 127.0.0.1 the kernel completes the
// TCP handshake on connect() and queues the connection on the listener's accept
// backlog, so we connect() BEFORE accept() and accept() returns immediately. The
// payloads are tiny (fit the send buffer), so send()/sendall() never block on a
// reader. Ports are OS-assigned (bind to port 0) so nothing depends on a fixed
// port or external network. Output is only booleans + fixed labels, so it is
// fully deterministic and asserted byte-for-byte. The harness runs the program
// under `timeout` so a regression that blocks fails fast instead of hanging.

#include "e2e_harness.hpp"


TEST(StdlibE2E, Socket) {
    e2e::expect_e2e("socket_sys", R"PURR(import io
import socket

# ---- low-level BSD path: socket/set_reuseaddr/bind/listen/local_port ----
let srv = socket.socket()
let reuse = socket.set_reuseaddr(srv)
let bound = socket.bind(srv, "127.0.0.1", 0)
let lstn = socket.listen(srv, 8)
let port = socket.local_port(srv)

# client socket, connect BEFORE accept (kernel queues the loopback handshake)
let cli = socket.socket()
let conn = socket.connect(cli, "127.0.0.1", port)
let peer = socket.accept(srv)

# ---- per-connection I/O: send + sendall + recv ----
let s1 = socket.send(cli, "AB")
let got1 = socket.recv(peer, 16)
let s2 = socket.sendall(peer, "PONG")
let got2 = socket.recv(cli, 16)

# ---- high-level convenience path: tcp_listen / tcp_connect ----
let hl_srv = socket.tcp_listen("127.0.0.1", 0, 8)
let hl_port = socket.local_port(hl_srv)
let hl_cli = socket.tcp_connect("127.0.0.1", hl_port)
let hl_peer = socket.accept(hl_srv)
let hl_s = socket.sendall(hl_cli, "hi")
let hl_got = socket.recv(hl_peer, 16)

# ---- deliberate failure + last_error ----
let fail = socket.connect(-1, "127.0.0.1", 1)
let err = socket.last_error()

# ---- verdicts (deterministic) ----
io.print("socket_ok", srv >= 0)
io.print("reuse_ok", reuse == 0)
io.print("bind_ok", bound == 0)
io.print("listen_ok", lstn == 0)
io.print("port_ok", port > 0)
io.print("connect_ok", conn == 0)
io.print("accept_ok", peer >= 0)
io.print("send_ok", s1 == 2)
io.print("recv1_ok", got1 == "AB")
io.print("sendall_ok", s2 == 0)
io.print("recv2_ok", got2 == "PONG")
io.print("tcp_listen_ok", hl_srv >= 0)
io.print("tcp_connect_ok", hl_cli >= 0)
io.print("hl_roundtrip_ok", hl_got == "hi")
io.print("fail_ok", fail < 0)
io.print("last_error_ok", err != "")

let c1 = socket.close(peer)
let c2 = socket.close(cli)
let c3 = socket.close(srv)
socket.close(hl_peer)
socket.close(hl_cli)
socket.close(hl_srv)
io.print("close_ok", c1 == 0, c2 == 0, c3 == 0)
)PURR",
               "socket_ok True\n"
               "reuse_ok True\n"
               "bind_ok True\n"
               "listen_ok True\n"
               "port_ok True\n"
               "connect_ok True\n"
               "accept_ok True\n"
               "send_ok True\n"
               "recv1_ok True\n"
               "sendall_ok True\n"
               "recv2_ok True\n"
               "tcp_listen_ok True\n"
               "tcp_connect_ok True\n"
               "hl_roundtrip_ok True\n"
               "fail_ok True\n"
               "last_error_ok True\n"
               "close_ok True True True\n");
}
