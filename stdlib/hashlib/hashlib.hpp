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

/** SHA-256 digest of @p data. @param data the bytes to hash (an embedded NUL is part of the input, since the length is carried). @return a 64-char lowercase hex digest.
 *  @note O(n) in the input length; allocates the 64-char result string and a padded message buffer internally. @test CheatahHashlib.KnownVectors, CheatahHashlib.DigestShape, CheatahHashlib.EmbeddedNulIsHashed */
std::string sha256(std::string_view data);  // 64-char lowercase hex digest

} // namespace cheatah::hashlib
