# `tls` — a from-scratch TLS 1.3 client

A minimal, dependency-free **TLS 1.3 client** (RFC 8446) built entirely on cheatah's own
crypto modules — `x25519` key exchange, the `aead` ChaCha20-Poly1305 (and AES-GCM) record
cipher, and `hashlib`'s HKDF key schedule. **No OpenSSL.**

## Server authentication (MITM-resistant by default)

The client **authenticates the server** — it is not just encryption. After the handshake proves
the peer holds the private key for the certificate it presents (CertificateVerify + Finished
MAC), the presented **X.509 chain is validated** (from-scratch, `x509.hpp`):

1. **Chain of trust** — each certificate is signed by the next, up to a certificate in the
   **system CA trust store** (`/etc/ssl/certs/…`, `$SSL_CERT_FILE`, or a caller-supplied bundle),
   with intermediates required to carry `basicConstraints: CA`. A self-signed or
   unknown-CA certificate is **refused**.
2. **Hostname** — the requested host must match the leaf's `subjectAltName` dNSNames (RFC 6125,
   with single left-label wildcards). A valid certificate for the *wrong* host is refused.
3. **Validity** — every certificate's `notBefore … notAfter` window must contain the current
   time. Expired / not-yet-valid certificates are refused.

Chain signatures are verified for **RSA PKCS#1 v1.5 (SHA-256)**, **ECDSA P-256 (SHA-256)**, and
**Ed25519**. Algorithms cheatah does not implement yet (e.g. ECDSA **P-384**, SHA-384/512)
**fail closed** — the connection is refused, never accepted unverified.

**Opting out (pinned / controlled peer).** For a server whose identity you establish out of
band, pass `insecure = true` to skip validation (leaf-key possession only), or `ca_file` to
trust a specific PEM bundle (e.g. a private CA or a pinned self-signed cert):

```python
with tls.open(sock.fd(), "example.com") as conn { … }                 # validate (default)
with tls.open(sock.fd(), "10.0.0.5", true) as conn { … }              # insecure: skip validation
with tls.open(sock.fd(), "internal.host", false, "/etc/my-ca.pem") { … }  # trust a private CA
```

`requests` and `websocket` ride this and inherit it: `requests.get("https://…")` and
`websocket.open_url("wss://…")` validate the certificate by default (with the same
`insecure` / `ca_file` options).

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
TLS_AES_128_GCM_SHA256, X25519 key share, SNI, and X.509 chain + hostname + expiry validation
(RSA-PKCS1-SHA256 / ECDSA-P256-SHA256 / Ed25519 chain signatures). Not yet: ECDSA P-384 and
SHA-384/512 chain signatures (refused, not accepted), certificate revocation (OCSP/CRL), and
client certificates.
