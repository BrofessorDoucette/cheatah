# cheatah `socket` 🐆

A small wrapper around BSD/POSIX **TCP sockets**, in the spirit of Python's
`socket` module. Two layers are available: a **flat, file-descriptor API** (like the
C layer — you pass the integer `fd` from `socket()` / `tcp_listen()` / `accept()` to
the other calls), and, on top of it, **owning guards** (`socket.open`, `socket.serve`)
that return `Conn`/`Listener` values whose destructors close the fd — use them with
`with` so a connection or listener can't leak (see below). IPv4 + TCP only; hosts are
resolved with `getaddrinfo`, so `"localhost"`, `"127.0.0.1"`, and DNS names all work.

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

- **Owning guards (RAII, `with`-friendly)** — `open(host, port)` → a `Conn`, and
  `serve(host, port, backlog)` → a `Listener`; each closes its fd on scope exit.
- **Convenience** — `tcp_listen(host, port, backlog)`, `tcp_connect(host, port)`.
- **Per-connection I/O** — `accept`, `recv`, `send`, `sendall`, `close`, plus
  `set_timeout(fd, ms)` (recv/send deadlines) and `shutdown(fd)` (half-close).
- **Low-level BSD** — `socket`, `set_reuseaddr`, `bind`, `listen`, `connect`,
  `local_port`, and `last_error` (the current `errno` text).

```python
import io
import socket

with socket.serve("127.0.0.1", 8080, 16) as server {   # a Listener guard
    io.print("listening on", server.local_port())
    with server.accept() as conn {                     # a Conn guard for the client
        let request = conn.recv(8192)
        let nl = chr(13) + chr(10)
        conn.sendall("HTTP/1.1 200 OK" + nl + "Content-Length: 2" + nl + nl + "hi")
    }                                                  # conn closed here, on every path
}                                                      # server closed here
```

Errors are reported as a negative return (or `""` from `recv`); call `last_error()`
for the message. `send` uses `MSG_NOSIGNAL`, so a broken pipe never raises a signal.
Only `recv` and `last_error` allocate (their returned string).

> Secure clients are built on this: the from-scratch [`tls`](../tls/) 1.3 client rides a
> connected socket, and [`requests`](../requests/) (pure cheatah) and
> [`websocket`](../websocket/) are layered on top. Plain `http://` works directly here.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[socket.hpp](socket.hpp). Tested in [../tests/socket_test.cpp](../tests/socket_test.cpp)
(real loopback round-trips); ASan + Valgrind clean via the QA gate
(`security/run-valgrind.sh`).
