# cheatah `socket` 🐆

A small wrapper around BSD/POSIX **TCP sockets**, in the spirit of Python's
`socket` module. Because cheatah has no
methods yet, the API is **flat and file-descriptor based** (like the C layer): you
pass the integer `fd` from `socket()` / `tcp_listen()` / `accept()` to the other
calls. IPv4 + TCP only; hosts are resolved with `getaddrinfo`, so `"localhost"`,
`"127.0.0.1"`, and DNS names all work.

```python
import io
import socket

let lfd = socket.tcp_listen("127.0.0.1", 8080, 16)   # create + bind + listen
io.print("listening on", socket.local_port(lfd))
let conn = socket.accept(lfd)                         # wait for a client
let request = socket.recv(conn, 8192)
let nl = chr(13) + chr(10)                            # CRLF (no `\r` string escape)
socket.sendall(conn, "HTTP/1.1 200 OK" + nl + "Content-Length: 2" + nl + nl + "hi")
socket.close(conn)
socket.close(lfd)
```

## What's here

- **Convenience** — `tcp_listen(host, port, backlog)`, `tcp_connect(host, port)`.
- **Per-connection I/O** — `accept`, `recv`, `send`, `sendall`, `close`.
- **Low-level BSD** — `socket`, `set_reuseaddr`, `bind`, `listen`, `connect`,
  `local_port`, and `last_error` (the current `errno` text).

Errors are reported as a negative return (or `""` from `recv`); call `last_error()`
for the message. `send` uses `MSG_NOSIGNAL`, so a broken pipe never raises a signal.
Only `recv` and `last_error` allocate (their returned string).

> This is the foundation for a future `requests`-style HTTP client. HTTPS/TLS is
> out of scope (it would need a TLS library); plain `http://` works today.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[socket.hpp](socket.hpp). Tested in [../tests/socket_test.cpp](../tests/socket_test.cpp)
(real loopback round-trips); ASan + Valgrind clean via the QA gate
(`security/run-valgrind.sh`).
