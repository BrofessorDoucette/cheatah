# cheatah `p256` 🐆

**NIST P-256 (secp256r1)** elliptic curve — ECDSA signature **verification** and
deterministic (**RFC 6979**) **signing**, plus the DER/SPKI parsing needed to use
it with TLS certificates and JWTs. From scratch, no external libraries.

This is the curve the rest of the world's TLS certificates and OAuth/JWT (ES256)
are signed with. Adding it lets cheatah's [`tls`](../tls/) client verify a real
server's `ecdsa_secp256r1_sha256` certificate (most of the public web), and lets
applications sign an ES256 JWT.

```purr
import io
import hashlib
import p256

let privkey = hashlib.from_hex("c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721")
let pubkey_xy = p256.public_from_private(privkey)
let h = hashlib.sha256_digest("the message")
let sig = p256.sign_raw(privkey, h)        # 64-byte r||s (RFC 6979, deterministic)
io.print(p256.verify_raw(pubkey_xy, h, sig))   # True
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
runs once per P-256 link in the certificate chain plus once for CertificateVerify —
negligible next to a network round trip; signing is the per-message JWT path.

## Security notes

ECDSA **verification** handles public data and stays branchy. **Signing** uses a deterministic
nonce (RFC 6979), which removes the "repeated/biased `k`" failure mode, and multiplies the
secret scalar with branch-free point ops and masked table selection. The limb arithmetic
underneath is branch-free too: each modular add, subtract and multiply computes its conditional
reduction unconditionally and selects the result with a mask, and the multi-limb compare is one
full-width subtract rather than a scan that stops at the first differing limb — the scan's
running time revealed how much of the private key matched the group order.

The differential in `CheatahP256.ConstantTimeFieldOpsMatchReference` checks that arithmetic
against a plainly written reference, because the point-op self-check runs both of its sides
through the same field operations and so cannot see an error in them.
