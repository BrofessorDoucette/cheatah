# cheatah `ed25519`

Public-key signatures (RFC 8032 Ed25519), implemented from scratch with no external
crypto dependency. A secret seed signs; the derived public key verifies; a verifier
holding only the public key cannot forge. Keys and signatures are lowercase hex; the
message is raw bytes.

```purr
import io
import ed25519

let secret = ed25519.generate()           # fresh 32-byte seed (hex)
let pub = ed25519.public_key(secret)       # derive the public key (hex)
let sig = ed25519.sign(secret, "release v1")
io.print(ed25519.verify(pub, "release v1", sig))   # True
io.print(ed25519.verify(pub, "tampered", sig))     # False
```

## Functions

- `generate()` — a fresh 32-byte secret seed (64 hex chars) from the OS CSPRNG. Keep
  it secret; anyone with it can sign as you.
- `public_key(secret_hex)` — derive the 32-byte public key (hex) from a secret seed.
- `sign(secret_hex, message)` — a 64-byte signature (hex) over `message` (raw bytes).
  Deterministic: the same seed and message always produce the same signature.
- `verify(public_hex, message, signature_hex)` — `true` iff the signature is valid.
  Touches only public data, rejects non-canonical signatures (`S ≥ L`), and returns
  `false` (never raises) on malformed input.

The implementation is validated byte-for-byte against the RFC 8032 known-answer
vectors and against OpenSSL. SHA-512 comes from the `hashlib` module (linked
automatically). This is the same code the cheatah runtime links to **verify a
compiled module** before loading it — see [SECURITY](../../docs/security.md).

Per-function docs (parameters, complexity, heap behavior) are in
[ed25519.hpp](ed25519.hpp). Tested in [../tests/ed25519_test.cpp](../tests/ed25519_test.cpp);
ASan + Valgrind clean via the QA gate.
