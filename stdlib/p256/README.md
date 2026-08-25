# cheatah `p256` 🐆

**NIST P-256 (secp256r1)** elliptic curve — ECDSA signature **verification** and
deterministic (**RFC 6979**) **signing**, plus the DER/SPKI parsing needed to use
it with TLS certificates and JWTs. From scratch, no external libraries.

This is the curve the rest of the world's TLS certificates and OAuth/JWT (ES256)
are signed with. Adding it lets cheatah's [`tls`](../tls/) client verify a real
server's `ecdsa_secp256r1_sha256` certificate (most of the public web), and lets
applications sign an ES256 JWT.

```python
import io
import hashlib
import p256

let h = hashlib.sha256_digest("the message")
let sig = p256.sign_raw(privkey, h)        # 64-byte r||s (RFC 6979, deterministic)
io.print(p256.verify_raw(pubkey_xy, h, sig))   # true
```

## What's inside

- **Field/scalar arithmetic** in Montgomery form over both P-256 moduli (the
  field prime `p` and the group order `n`); the Montgomery constants are derived
  from the modulus at startup, so there are no hand-transcribed magic numbers.
- **Points** in Jacobian coordinates (`a = -3`).
- **Verify** uses **Strauss-Shamir** (one doubling chain for `u1·G + u2·Q`).
- **Sign** uses an RFC 6979 deterministic nonce (HMAC-SHA256 — no entropy source
  needed, never repeats a nonce) and a **fixed-base comb** for `k·G`.
- **Parsing:** DER `SEQUENCE{r,s}` (the TLS/X.509 form), raw `r||s` (the JWT
  ES256 form), and the uncompressed EC point out of a certificate's SPKI.

Byte conventions: scalars/coordinates are 32 big-endian bytes; a public key point
is the 64 bytes `X||Y`; a raw signature is the 64 bytes `r||s`.

## Correctness & speed

Verified against the **RFC 6979 Appendix A.2.5** P-256/SHA-256 test vector — the
deterministic signature matches bit-for-bit, and verification round-trips it
(`stdlib/tests/p256_test.cpp`). Micro-benchmarks (`tests/benchmarks/p256_bench.cpp`,
release build, one core) are on the [p256 benchmarks](BENCHMARKS.md) page — verification
runs once per TLS handshake (negligible next to a network round trip); signing is the
per-message JWT path.

## Security notes

ECDSA **verification** here handles public data. **Signing** uses a deterministic
nonce (RFC 6979), which removes the catastrophic "repeated/biased `k`" failure
mode. The scalar routines are written straightforwardly for correctness; they are
not yet hardened to be fully constant-time against a local timing attacker, which
matters only when signing with a long-lived private key on a shared host.
