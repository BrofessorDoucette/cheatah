# cheatah `p384` 🐆

**NIST P-384 (secp384r1)** elliptic curve — ECDSA signature **verification**,
plus the SPKI parsing needed to use it with TLS certificates. From scratch, no
external libraries.

P-384 is the curve the big CAs' **issuing chains** are signed with (Sectigo,
DigiCert, GlobalSign ECC roots): a typical CDN-fronted host serves a P-256 leaf
whose intermediate and root signatures are `ecdsa-with-SHA384` under P-384 keys.
Adding it lets cheatah's [`tls`](../tls/) client validate those real chains
(api.github.com, Fastly-fronted hosts, …) and P-384 leaf certificates
(`ecdsa_secp384r1_sha384` CertificateVerify).

<!-- purr: fragment -->
```purr
import io
import hashlib
import p384

let h = hashlib.sha384_digest("the message")
io.print(p384.verify_raw(pubkey_xy, h, sig))   # True
```

## What's inside

- The **width-generic Weierstrass core** shared with [`p256`](../p256/)
  (`p256/ec_core.hpp`): Montgomery field/scalar arithmetic with startup-derived
  constants, Jacobian points (`a = -3`), and **Strauss-Shamir** verification —
  the same battle-tested code p256 runs, instantiated at 6×64-bit limbs.
- **Parsing:** DER `SEQUENCE{r,s}` (the TLS/X.509 form), raw `r||s` (the JWT
  ES384 form), and the uncompressed EC point out of a certificate's SPKI.

Byte conventions: scalars/coordinates are 48 big-endian bytes; a public key point
is the 96 bytes `X||Y`; a raw signature is the 96 bytes `r||s`.

## Correctness & security

Verified against the **RFC 6979 Appendix A.2.6** P-384 test vectors (SHA-384 and
SHA-256, pinning both hash-truncation semantics) and cross-checked through the
X.509 suite against real OpenSSL-minted certificates
(`stdlib/tests/p384_test.cpp`, `stdlib/tests/x509_test.cpp`).

**Verify-only by design**: certificate validation handles PUBLIC data, which is
all TLS needs from this curve — there is no private key here to protect, so the
straightforward (not constant-time) scalar routines are the right trade. cheatah's
own TLS server signs with Ed25519, and JWT signing uses P-256/ES256.
