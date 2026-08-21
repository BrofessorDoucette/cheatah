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

### Throughput tuning (default on every connected socket)

Every socket returned by `connect`/`tcp_connect` and `accept` is tuned for bulk transfer with **no
caller opt-in**: `TCP_NODELAY` (Nagle off), `TCP_QUICKACK` (prompt ACKs — re-armed after each
`recv`, since Linux clears it), and an enlarged `SO_RCVBUF`/`SO_SNDBUF`. Without these a pure
download sits in delayed-ACK slow start — one segment per round-trip — which crawls even though the
CPU is idle. Guarded so `getsockopt` proves the options on both ends
(`socket_test.cpp` `ConnectedSocketsAreTunedByDefault`).

**Measured against known-fast references** (2026-07-18; `scripts/net_bench_compare.sh`,
`scripts/tls_loopback_bench.sh`):
- On a throttled WAN link, a cheatah HTTPS GET reaches **0.90× curl** on the identical URL
  (parity — both pinned by the throttle; the download path itself is not the limiter).
- Throttle-free over loopback vs a real `openssl s_server`, cheatah TLS sustains <b>~210 MB/s</b>
  (byte-identity verified) — far above any real link. curl hits ~1500 MB/s there; the remaining gap
  is per-record TLS framing (copies + per-record key re-expansion), **not** the cipher: cheatah
  AES-128-GCM benches at **3.56 GiB/s against OpenSSL's 3.41 — a tie**, not a win (1.04× is well
  inside the 1.15× band we require before calling anything faster; see
  [the crypto table](../../docs/performance.md#vs-openssl), re-measured 2026-08-19 over 9
  interleaved repetitions). Closing the framing gap only matters above ~200 MB/s links and is
  tracked as evidence-gated follow-up.

> Secure clients are built on this: the from-scratch [`tls`](../tls/) 1.3 client rides a
> connected socket, and [`requests`](../requests/) (pure cheatah) and
> [`websocket`](../websocket/) are layered on top. Plain `http://` works directly here.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[socket.hpp](socket.hpp). Tested in [../tests/socket_test.cpp](../tests/socket_test.cpp)
(real loopback round-trips); ASan + Valgrind clean via the QA gate
(`security/run-valgrind.sh`).
