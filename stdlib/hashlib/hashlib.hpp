// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

/**
 * HMAC-SHA-256 (RFC 2104) over raw bytes — the PRF under HKDF and the TLS 1.3 key schedule.
 *
 * @param key raw key bytes (any length; hashed down when longer than the block).
 * @param data raw message bytes.
 * @return the 32-byte MAC as raw bytes.
 * @complexity O(|key| + |data|).
 * @alloc the returned 32-byte digest plus fixed key/inner/outer scratch buffers.
 * @test CheatahHashlib.HmacSha256
 * @crtest HashlibCompileRun.Hmac
 * @systest StdlibE2E.Hashlib
 */
std::string hmac_sha256(std::string_view key, std::string_view data);

/**
 * HMAC-SHA-512 (RFC 2104) over raw bytes — the wider PRF (128-byte block, 64-byte MAC), e.g. for
 * authentication schemes that mandate SHA-512.
 *
 * @param key raw key bytes (any length; hashed down when longer than the 128-byte block).
 * @param data raw message bytes.
 * @return the 64-byte MAC as raw bytes.
 * @complexity O(|key| + |data|).
 * @alloc the returned 64-byte digest plus fixed key/inner/outer scratch buffers.
 * @test CheatahHashlib.HmacSha512
 * @crtest HashlibCompileRun.Hmac
 * @systest StdlibE2E.Hashlib
 */
std::string hmac_sha512(std::string_view key, std::string_view data);

/**
 * Base64 ENCODE (RFC 4648, standard alphabet `A–Za–z0–9+/`, `=` padding) of raw bytes to ASCII.
 *
 * @param data the raw bytes to encode (embedded NULs are encoded).
 * @return the base64 text (length `4*ceil(n/3)`).
 * @complexity O(n).
 * @alloc the returned string.
 * @test CheatahHashlib.Base64KnownVectors, CheatahHashlib.Base64RoundTrip
 * @crtest HashlibCompileRun.Base64
 * @systest StdlibE2E.Hashlib
 */
std::string base64_encode(std::string_view data);

/**
 * Base64 DECODE (RFC 4648 standard alphabet) of ASCII to the raw bytes. Whitespace/newlines are
 * skipped; decoding stops at the first `=` pad; non-alphabet bytes are ignored (lenient, like
 * Python's `base64.b64decode` on a clean stream).
 *
 * @param text the base64 text.
 * @return the decoded raw bytes (may contain embedded NULs).
 * @complexity O(|text|).
 * @alloc the returned string.
 * @test CheatahHashlib.Base64KnownVectors, CheatahHashlib.Base64RoundTrip
 * @crtest HashlibCompileRun.Base64
 * @systest StdlibE2E.Hashlib
 */
std::string base64_decode(std::string_view text);

/**
 * HKDF-Extract (RFC 5869): PRK = HMAC-SHA-256(salt, ikm).
 *
 * @param salt the (optional) salt; pass "" for the all-zero default salt.
 * @param ikm the input keying material.
 * @return the 32-byte pseudorandom key (PRK) as raw bytes.
 * @complexity O(|salt| + |ikm|).
 * @alloc the returned 32-byte PRK plus fixed HMAC scratch buffers.
 * @test CheatahHashlib.HkdfRfc5869Case1
 * @crtest HashlibCompileRun.Hkdf
 * @systest StdlibE2E.Hashlib
 */
std::string hkdf_extract(std::string_view salt, std::string_view ikm);

/**
 * HKDF-Expand (RFC 5869): derive @p length bytes of keying material from @p prk and @p info.
 *
 * @param prk a pseudorandom key (e.g. from hkdf_extract()).
 * @param info optional context/application-specific info binding the output ("" for none).
 * @param length the number of output-keying-material bytes to produce.
 * @return the OKM as raw bytes, or "" when length is 0 or exceeds 255*32 (the RFC bound).
 * @complexity O(length).
 * @alloc the returned keystream plus one HMAC scratch buffer per 32-byte output block.
 * @test CheatahHashlib.HkdfRfc5869Case1
 * @crtest HashlibCompileRun.Hkdf
 * @systest StdlibE2E.Hashlib
 */
std::string hkdf_expand(std::string_view prk, std::string_view info, long long length);

} // namespace cheatah::hashlib
