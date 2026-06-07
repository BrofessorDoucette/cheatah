# cheatah `hashlib`

Cryptographic hashing, mirroring the core of Python's `hashlib`. Provides a
self-contained SHA-256 (no external crypto dependency).

```purr
import hashlib

hashlib.sha256("abc")
# "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
```

## Functions

- `sha256(data)` — SHA-256 of `data`, returned as a 64-character lowercase hex
  digest. The input length is carried, so an embedded NUL byte is hashed as part
  of the data.

The implementation follows FIPS-180 and is checked against the standard
NIST SHA-256 test vectors (including the multi-block length-padding edge case).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[hashlib.hpp](hashlib.hpp). Tested in
[../tests/hashlib_test.cpp](../tests/hashlib_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
