# cheatah `aead`

Authenticated encryption with associated data — ChaCha20-Poly1305 (RFC 8439) and
AES-128-GCM, self-contained (no OpenSSL). These are the record ciphers behind the
`tls` module and are equally usable directly. AES-GCM takes a runtime-selected AES-NI +
PCLMULQDQ fast path on capable x86 CPUs, with a portable scalar fallback everywhere else
— identical results either way.

```purr
import aead

let ct = aead.chacha20poly1305_encrypt(key_hex, nonce_hex, aad, "attack at dawn")
let pt = aead.chacha20poly1305_decrypt(key_hex, nonce_hex, aad, ct)
```

## Functions

- `chacha20poly1305_encrypt(key_hex, nonce_hex, aad, plaintext)` /
  `chacha20poly1305_decrypt(key_hex, nonce_hex, aad, ciphertext)` — RFC 8439 AEAD;
  decrypt returns `""` on an authentication failure (a tampered message never
  yields plaintext).
- `aes128gcm_encrypt(key_hex, nonce_hex, aad, plaintext)` /
  `aes128gcm_decrypt(key_hex, nonce_hex, aad, ciphertext)` — AES-128-GCM, same
  contract.
- `crypto_hardware_active()` — whether AES-GCM is currently on the hardware
  (AES-NI) path; lets a deployment assert which implementation it exercises.

Keys and nonces are lowercase hex; plaintext/ciphertext/aad are raw bytes
(NUL-safe, length-carried). Checked against the RFC 8439 and NIST GCM test
vectors, with the portable and hardware paths cross-checked against each other.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[aead.hpp](aead.hpp). Tested in [../tests/aead_test.cpp](../tests/aead_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
