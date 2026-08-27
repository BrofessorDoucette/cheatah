# cheatah `hashlib`

Cryptographic hashing and keyed primitives — self-contained SHA-256/384/512, HMAC,
HKDF, hex and Base64, with no external crypto dependency. Each hash comes in a hex form
and a raw-bytes form (like a digest vs hexdigest). These primitives back the `tls`
and `ed25519` modules and are equally usable directly.

```purr
import hashlib

hashlib.sha256("abc")
# "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
```

## Functions

- `sha256(data)`, `sha384(data)`, `sha512(data)` — lowercase hex digests of 64, 96
  and 128 characters.
- `sha256_digest`, `sha384_digest`, `sha512_digest` — the raw 32 / 48 / 64 bytes;
  `sha512_digest` backs the `ed25519` module (RFC 8032).
- `hmac_sha256` / `hmac_sha384` / `hmac_sha512(key, data)` — RFC 2104 keyed MAC, raw bytes.
- `hkdf_extract(salt, ikm)` / `hkdf_expand(prk, info, length)`, plus the `_sha384`
  pair — RFC 5869 key derivation (the TLS 1.3 key schedule is built on these).
- `base64_encode(data)` / `base64_decode(text, strict=false)` — Base64 (RFC 4648);
  `to_hex(bytes)` / `from_hex(hex)` — lowercase hex.

The input length is carried, so an embedded NUL byte is hashed as part of the data.
The implementation follows FIPS-180 and is checked against the standard NIST SHA-256
and SHA-512 test vectors (including the multi-block length-padding edge case).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[hashlib.hpp](hashlib.hpp). Tested in
[../tests/hashlib_test.cpp](../tests/hashlib_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
