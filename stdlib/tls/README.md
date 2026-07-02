# `tls` — a from-scratch TLS 1.3 client

A minimal, dependency-free **TLS 1.3 client** (RFC 8446) built entirely on cheatah's own
crypto modules — `x25519` key exchange, the `aead` ChaCha20-Poly1305 (and AES-GCM) record
cipher, and `hashlib`'s HKDF key schedule. **No OpenSSL.** The server's certificate is
authenticated (Ed25519 via `ed25519`, ECDSA P-256 via `p256`, or RSA-PSS via `rsa_verify`);
a server it cannot authenticate is **refused**, never left unverified.

```python
import socket
import tls

# tls rides an already-connected TCP socket. Both are owning guards, so nothing leaks.
with socket.open("example.com", 443) as sock {
    with tls.open(sock.fd(), "example.com") as conn {
        conn.send("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
        io.print(conn.recv(65536))
    }
}
```

## API (cheatah-facing)

- **`tls.open(fd, server_name) -> Conn`** — run the handshake over a connected fd; returns an
  owning `Conn`. On failure `conn.is_open()` is false (see `tls.last_error()`).
- **`Conn`** methods: `send(data)`, `recv(bufsize)`, `shutdown()`, `close()`, `is_open()`,
  `id()`. The `Conn` sends `close_notify` and erases its session automatically at scope exit —
  held as a plain `let` or in a `with`, it cannot leak.
- **`tls.last_error()`** — the last error message on this thread.

> The underlying TCP fd is **not** owned by the TLS session — guard it with a `socket.Conn`
> (as above) or close it yourself.

## Note for C++ callers

The flat, handle-based API (`client_connect`/`send`/`recv`/`close`, keyed by an integer
session id) is **C++-only** and lives in `tls_lowlevel.hpp`. It is intentionally not reachable
from cheatah, so cheatah code cannot leak a session — it uses the `tls.Conn` guard instead.

**Scope (v1):** TLS 1.3 only, cipher suites TLS_CHACHA20_POLY1305_SHA256 and
TLS_AES_128_GCM_SHA256, X25519 key share, SNI. Full X.509 chain validation beyond the leaf
signature is not yet implemented — pin or control the peer.
