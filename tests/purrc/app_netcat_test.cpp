// System-level "application" test: a small TCP round-trip ("netcat"-style) app
// that only passes if socket + string + io all cooperate end to end.
//
// This is deliberately a multi-module integration program, not a single-function
// probe: it stands up a real loopback TCP connection, builds a message with the
// `string` and `io` modules, ships it over the socket, reads it back on the peer
// fd, and verifies byte-exactness -- all inside a SINGLE cheatah process.
//
// Why it doesn't deadlock in one process: for 127.0.0.1 the kernel completes the
// TCP handshake on connect() and queues the connection on the listener's accept
// backlog, so connect() returns before accept() is called and the subsequent
// accept() returns immediately. The payload is small enough to fit in the socket
// send buffer, so sendall() does not block waiting for a reader. Output is fully
// deterministic (only booleans + fixed labels), so it can be asserted
// byte-for-byte.
//
// Modules exercised: socket (tcp_listen/local_port/tcp_connect/accept/sendall/
// recv/close), string (upper, concat), io (str, print), plus the builtin len().

#include "e2e_harness.hpp"


TEST(SystemApps, NetworkRoundtrip) {
    e2e::expect_e2e("app_netcat", R"PURR(import io
import string
import socket

let listener = socket.tcp_listen("127.0.0.1", 0, 8)
let port = socket.local_port(listener)
let client = socket.tcp_connect("127.0.0.1", port)
let server = socket.accept(listener)

let payload = string.upper("ping") + " " + io.str(port > 0)
let sent = socket.sendall(client, payload)
let got = socket.recv(server, 1024)

io.print("listen_ok", listener >= 0)
io.print("port_ok", port > 0)
io.print("connect_ok", client >= 0)
io.print("accept_ok", server >= 0)
io.print("send_ok", sent == 0)
io.print("len_match", len(got) == len(payload))
io.print("bytes_match", got == payload)

socket.close(server)
socket.close(client)
socket.close(listener)
)PURR",
               "listen_ok True\n"
               "port_ok True\n"
               "connect_ok True\n"
               "accept_ok True\n"
               "send_ok True\n"
               "len_match True\n"
               "bytes_match True\n");
}
