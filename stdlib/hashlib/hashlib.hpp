#pragma once

/**
 * @file hashlib.hpp
 * @brief cheatah `hashlib` — cryptographic hashing, mirroring the core of
 *        Python's `hashlib`. `import hashlib` to use it.
 *
 * `import hashlib` includes this header AND links `libcheatah_hashlib`. Provides
 * self-contained SHA-256 and SHA-512 (no external crypto dependency), each in a
 * hex form (`sha256`/`sha512`, like Python's `.hexdigest()`) and a raw-bytes form
 * (`sha256_digest`/`sha512_digest`, like Python's `.digest()`). The raw forms back
 * the `ed25519` module and the runtime's module-integrity check. Unit tests:
 * `stdlib/tests/hashlib_test.cpp`; the suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 */
#include <string>
#include <string_view>

namespace cheatah::hashlib {

/**
 * SHA-256 digest of @p data, as hex.
 *
 * Computes the full SHA-256 of the byte view (standard padding plus 64-bit
 * big-endian length) and formats the 32-byte hash as hex. Hashing the empty
 * string is well-defined and returns the canonical
 * `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.
 * @param data the bytes to hash (an embedded NUL is part of the input, since the length is
 *   carried).
 * @return a 64-char lowercase hex digest.
 * @complexity O(n) in the input length.
 * @alloc allocates the 64-char result string and a padded message buffer internally.
 * @test CheatahHashlib.KnownVectors, CheatahHashlib.DigestShape,
 *   CheatahHashlib.EmbeddedNulIsHashed
 * @crtest HashlibCompileRun.Sha256
 * @systest StdlibE2E.Hashlib
 */
std::string sha256(std::string_view data);  // 64-char lowercase hex digest

/**
 * SHA-512 digest of @p data, as hex.
 *
 * The full SHA-512 (128-byte blocks, 80 rounds, 128-bit length field) of the byte
 * view, formatted as hex. The empty string hashes to the canonical
 * `cf83e1357eefb8bd…3e85` (128 hex chars).
 * @param data the bytes to hash (an embedded NUL is part of the input).
 * @return a 128-char lowercase hex digest.
 * @complexity O(n) in the input length.
 * @alloc allocates the 128-char result string and a padded message buffer internally.
 * @test CheatahHashlib.Sha512KnownVectors, CheatahHashlib.Sha512DigestShape
 * @crtest HashlibCompileRun.Sha512
 * @systest StdlibE2E.Hashlib
 */
std::string sha512(std::string_view data);  // 128-char lowercase hex digest

/**
 * SHA-256 of @p data as the raw 32 bytes (Python's `.digest()`), not hex.
 * @param data the bytes to hash.
 * @return a 32-byte string (may contain embedded NULs).
 * @complexity O(n) in the input length.
 * @alloc allocates the 32-byte result and a padded message buffer.
 * @test CheatahHashlib.RawDigestMatchesHex
 * @crtest HashlibCompileRun.Sha256Digest
 * @systest StdlibE2E.Hashlib
 */
std::string sha256_digest(std::string_view data);  // 32 raw bytes

/**
 * SHA-512 of @p data as the raw 64 bytes (Python's `.digest()`), not hex. Backs the
 * `ed25519` module, which uses SHA-512 internally per RFC 8032.
 * @param data the bytes to hash.
 * @return a 64-byte string (may contain embedded NULs).
 * @complexity O(n) in the input length.
 * @alloc allocates the 64-byte result and a padded message buffer.
 * @test CheatahHashlib.RawDigestMatchesHex
 * @crtest HashlibCompileRun.Sha512Digest
 * @systest StdlibE2E.Hashlib
 */
std::string sha512_digest(std::string_view data);  // 64 raw bytes

} // namespace cheatah::hashlib
