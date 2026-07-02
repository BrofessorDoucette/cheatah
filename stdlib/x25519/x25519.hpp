#pragma once

/**
 * @file x25519.hpp
 * @brief cheatah `x25519` — the X25519 Diffie-Hellman function (RFC 7748), from scratch.
 *        `import x25519` to use it. The KEY-EXCHANGE half of Curve25519 (cheatah `ed25519`
 *        is the SIGNATURE half); together with `aead` and hashlib's HKDF it is the crypto
 *        core of the from-scratch cheatah TLS 1.3 client. No OpenSSL.
 *
 * Keys and points are 32 bytes, passed as 64-char lowercase hex (the ed25519 module's
 * convention). The field arithmetic is CONSTANT-TIME: no secret-dependent branches or
 * indices (the ladder uses arithmetic conditional swaps).
 */
#include <string>
#include <string_view>

namespace cheatah::x25519 {

/**
 * The X25519 function: scalar-multiply @p point_hex (a u-coordinate) by @p scalar_hex.
 * For Diffie-Hellman: shared = x25519(my_secret, their_public).
 *
 * @param scalar_hex 64-char hex (32-byte scalar; clamped per RFC 7748).
 * @param point_hex  64-char hex (32-byte u-coordinate).
 * @return the 32-byte result as 64-char hex, or "" on malformed input or an all-zero
 *         result (a contributory-behaviour check: a zero shared secret is REJECTED).
 * @complexity O(1) — 255 constant-time ladder steps.
 * @alloc the returned string.
 * @test CheatahX25519.Rfc7748Vector1
 * @crtest X25519CompileRun.SharedSecret
 * @systest StdlibE2E.X25519
 */
std::string x25519(std::string_view scalar_hex, std::string_view point_hex);

/**
 * The base-point form: derive the public key for @p scalar_hex (u = 9).
 *
 * @param scalar_hex 64-char hex secret key.
 * @return the public key as 64-char hex, or "" on malformed input.
 * @complexity O(1) — one ladder.
 * @alloc the returned string.
 * @test CheatahX25519.DiffieHellman
 * @crtest X25519CompileRun.PublicKey
 * @systest StdlibE2E.X25519
 */
std::string x25519_base(std::string_view scalar_hex);

} // namespace cheatah::x25519
