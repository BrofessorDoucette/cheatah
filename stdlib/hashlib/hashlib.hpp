#pragma once

/**
 * @file hashlib.hpp
 * @brief cheatah `hashlib` — cryptographic hashing, mirroring the core of
 *        Python's `hashlib`. `import hashlib` to use it.
 *
 * `import hashlib` includes this header AND links `libcheatah_hashlib`. Provides
 * a self-contained SHA-256 (no external crypto dependency) returning a lowercase
 * hex digest. Unit tests: `stdlib/tests/hashlib_test.cpp`; the suite runs under
 * AddressSanitizer (the `asan` preset) and Valgrind (`security/run-valgrind.sh`)
 * on every QA-gate run.
 */
#include <string>
#include <string_view>

namespace cheatah::hashlib {

/**
 * SHA-256 digest of @p data.
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

} // namespace cheatah::hashlib
