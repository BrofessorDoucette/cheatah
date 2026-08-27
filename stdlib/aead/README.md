# cheatah `aead`

Authenticated encryption with associated data — ChaCha20-Poly1305 (RFC 8439) and
AES-GCM with 128- or 256-bit keys, self-contained (no OpenSSL). These are the record ciphers behind the
`tls` module and are equally usable directly. AES-GCM takes the CPU's crypto
instructions when it has them (x86 AES-NI + PCLMULQDQ, ARMv8 AES + PMULL) and a portable
scalar path everywhere else — identical results either way.

```purr
import aead

let key_hex = "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
let nonce_hex = "070000004041424344454647"
let ct = aead.chacha20poly1305_encrypt(key_hex, nonce_hex, "header", "attack at dawn")
let pt = aead.chacha20poly1305_decrypt(key_hex, nonce_hex, "header", ct)
```

## Functions

- `chacha20poly1305_encrypt(key_hex, nonce_hex, aad, plaintext)` /
  `chacha20poly1305_decrypt(key_hex, nonce_hex, aad, ciphertext)` — RFC 8439 AEAD;
  decrypt returns `""` on an authentication failure (a tampered message never
  yields plaintext).
- `aes128gcm_encrypt(key_hex, nonce_hex, aad, plaintext)` /
  `aes128gcm_decrypt(key_hex, nonce_hex, aad, ciphertext)`, and the `aes256gcm_`
  pair for 32-byte keys — AES-GCM, same contract.
- `crypto_hardware_active()` — whether AES-GCM is currently on the hardware
  (AES-NI or ARMv8 AES) path; lets a deployment assert which implementation it exercises.

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
also async-signal-safe, and they run ahead of the string forms by exactly the two
allocations they skip.

Keys and nonces are lowercase hex; plaintext/ciphertext/aad are raw bytes
(NUL-safe, length-carried). Checked against the RFC 8439 and NIST GCM test
vectors, with the portable and hardware paths cross-checked against each other.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[aead.hpp](aead.hpp). Tested in [../tests/aead_test.cpp](../tests/aead_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
