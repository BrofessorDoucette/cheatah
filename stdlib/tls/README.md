# `tls` — a from-scratch TLS 1.3 client and server

A minimal, dependency-free **TLS 1.3** implementation (RFC 8446) — both a **client** and a
**server** handshake — built entirely on cheatah's own crypto modules: `x25519` key exchange,
the `aead` ChaCha20-Poly1305 (and AES-GCM) record cipher, `hashlib`'s HKDF key schedule, and
`ed25519` signatures. **No OpenSSL.** The handshake is validated against OpenSSL as the peer in
both directions (our client vs `openssl s_server`, our server vs `openssl s_client`).

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

Chain signatures are verified for **RSA PKCS#1 v1.5 (SHA-256 and SHA-384)**, **ECDSA with
SHA-256 or SHA-384 under P-256 or P-384 issuer keys** (the hash comes from the signature OID,
the curve from the issuer's key — real CA chains mix them), and **Ed25519**. Algorithms cheatah
does not implement yet (e.g. SHA-512, rsassa-PSS chain signatures) **fail closed** — the
connection is refused, never accepted unverified.

**Opting out (pinned / controlled peer).** For a server whose identity you establish out of
band, pass `insecure = true` to skip validation (leaf-key possession only), or `ca_file` to
trust a specific PEM bundle (e.g. a private CA or a pinned self-signed cert):

<!-- purr: fragment -->
```purr
with tls.open(sock.fd(), "example.com") as conn { … }                 # validate (default)
with tls.open(sock.fd(), "10.0.0.5", true) as conn { … }              # insecure: skip validation
with tls.open(sock.fd(), "internal.host", false, "/etc/my-ca.pem") { … }  # trust a private CA
```

`requests` and `websocket` ride this and inherit it: `requests.get("https://…")` and
`websocket.open_url("wss://…")` validate the certificate by default (with the same
`insecure` / `ca_file` options).

```purr
import io
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

- <b>`tls.open(fd, server_name) -> Conn`</b> — run the **client** handshake over a connected fd,
  authenticating the server; returns an owning `Conn`. On failure `conn.is_open()` is false
  (see `tls.last_error()`).
- <b>`tls.accept(fd, cert_pem, key_pem) -> Conn`</b> — run the **server** handshake over an accepted
  fd, presenting an **Ed25519 or ECDSA P-256** leaf and signing with its private `key_pem`
  (PKCS#8 Ed25519, or PKCS#8/SEC1 P-256 EC); returns an owning `Conn`. `cert_pem` may be a full
  chain (`fullchain.pem` — leaf first, then intermediates) and every block is sent, so a
  Let's Encrypt certificate works as issued and browsers get a path to their trust anchor. This
  is the "HTTPS with zero non-cheatah software" path — pair it with a `socket` accept loop.
- <b>`Conn`</b> methods: `send(data)`, `recv(bufsize)`, `shutdown()`, `close()`, `is_open()`,
  `id()`. The `Conn` sends `close_notify` and erases its session automatically at scope exit —
  held as a plain `let` or in a `with`, it cannot leak.
- <b>`tls.last_error()`</b> — the last error message on this thread.

> The TLS session does **not** own the TCP fd — guard it with a `socket.Conn` or close it yourself.

## Note for C++ callers

The flat, handle-based API (`client_connect`/`send`/`recv`/`close`, keyed by an integer
session id) is **C++-only** and lives in `tls_lowlevel.hpp`. It is intentionally not reachable
from cheatah, so cheatah code cannot leak a session — it uses the `tls.Conn` guard instead.

**Scope (v1):** TLS 1.3 only; the client offers TLS_AES_256_GCM_SHA384, TLS_AES_128_GCM_SHA256
and TLS_CHACHA20_POLY1305_SHA256 (AES first with AES-NI, ChaCha20 first without), X25519 key
share, SNI, and the X.509 chain + hostname + expiry validation above. The **server** side
presents an **Ed25519 or ECDSA P-256** leaf (P-256 is what public CAs issue, so a CA-trusted
HTTPS server needs no other software; full-chain PEMs are sent whole), signs per RFC 8446
§4.4.3 only with an algorithm the client offered, refuses a cert/key mismatch at startup with
a precise error, and speaks ChaCha20-Poly1305 when the client offers it, else AES-128-GCM.
Not yet: RSA or P-384 **server** certificates, SHA-512 chain signatures (refused, not
accepted), certificate revocation (OCSP/CRL), and client certificates.
