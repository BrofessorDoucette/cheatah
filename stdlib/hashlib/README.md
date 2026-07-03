# cheatah `hashlib`

Cryptographic hashing and keyed primitives — self-contained SHA-256/SHA-512, HMAC,
HKDF, and Base64, with no external crypto dependency. Each hash comes in a hex form
and a raw-bytes form (like a digest vs hexdigest). These primitives back the `tls`
and `ed25519` modules and are equally usable directly.

```purr
import hashlib

hashlib.sha256("abc")
# "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
```

## Functions

- `sha256(data)` — SHA-256 of `data` as a 64-character lowercase hex digest.
- `sha512(data)` — SHA-512 of `data` as a 128-character lowercase hex digest.
- `sha256_digest(data)` — the raw 32-byte SHA-256 (not hex).
- `sha512_digest(data)` — the raw 64-byte SHA-512 (not hex); backs the `ed25519`
  module, which uses SHA-512 internally per RFC 8032.
- `hmac_sha256(key, data)` / `hmac_sha512(key, data)` — RFC 2104 keyed MAC, raw bytes.
- `hkdf_extract(salt, ikm)` / `hkdf_expand(prk, info, length)` — RFC 5869 key
  derivation (the TLS 1.3 key schedule is built on these).
- `base64_encode(data)` / `base64_decode(text)` — standard Base64 (RFC 4648).

The input length is carried, so an embedded NUL byte is hashed as part of the data.
The implementation follows FIPS-180 and is checked against the standard NIST SHA-256
and SHA-512 test vectors (including the multi-block length-padding edge case).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[hashlib.hpp](hashlib.hpp). Tested in
[../tests/hashlib_test.cpp](../tests/hashlib_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
