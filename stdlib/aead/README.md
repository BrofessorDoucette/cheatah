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

### Allocation-free variants (C++ only)

- `chacha20poly1305_encrypt_into(key, nonce, aad, aad_len, plaintext, plaintext_len, out)` /
  `chacha20poly1305_decrypt_into(key, nonce, aad, aad_len, ciphertext, ciphertext_len, out)`

The functions above return a `std::string`, so they allocate their result and,
internally, a buffer to assemble the MAC input. That rules them out wherever
allocation is forbidden or simply unwelcome: signal handlers, embedded targets,
and hot paths that already own their memory. These take raw byte pointers and a
caller-provided output buffer, allocate **nothing**, and may encrypt in place
(`out` may alias the input). `out` needs `plaintext_len + 16` bytes on encrypt —
the ciphertext followed by the tag.

They are the same algorithm on the same code paths, not a second implementation:
the tag is streamed through Poly1305 segment by segment instead of over one
concatenated buffer, which is possible because the AEAD pads every segment to a
16-byte boundary. A test asserts byte-identical output to the string forms over
the RFC 8439 vector and 200 randomized sizes. Being allocation-free, they are
also async-signal-safe. Measured ~11% faster than the string forms (490 vs 438
MiB/s on the benchmark's payload) — exactly the two allocations they skip.

Keys and nonces are lowercase hex; plaintext/ciphertext/aad are raw bytes
(NUL-safe, length-carried). Checked against the RFC 8439 and NIST GCM test
vectors, with the portable and hardware paths cross-checked against each other.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[aead.hpp](aead.hpp). Tested in [../tests/aead_test.cpp](../tests/aead_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
