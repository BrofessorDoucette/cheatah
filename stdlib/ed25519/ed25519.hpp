// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file ed25519.hpp
 * @brief cheatah `ed25519` — public-key signatures (RFC 8032 Ed25519), implemented
 *        from scratch with NO external crypto dependency. `import ed25519` to use it.
 *
 * `import ed25519` includes this header AND links `libcheatah_ed25519` (which links
 * `libcheatah_hashlib` for the SHA-512 Ed25519 needs internally). Python has no
 * public-key crypto in its standard library, so this is a focused cheatah module,
 * sibling to `hashlib`: a secret seed signs, the derived public key verifies, and a
 * verifier holding only the public key cannot forge.
 *
 * Keys and signatures are passed as lowercase hex (printable, file-storable); the
 * MESSAGE is raw bytes. The cheatah runtime links this module to verify a `.so`'s
 * Ed25519 signature before loading it (`CHEATAH_VERIFY=strict`).
 *
 * SECURITY NOTES
 *  - `verify()` touches only public data (public key, message, signature) — no secret
 *    is handled, so there is no secret-dependent timing in the verify path.
 *  - `sign()` / `generate()` handle the secret seed; they are intended for OFFLINE use
 *    (the `purrc --sign` / `--keygen` tooling), not on an untrusted host.
 *  - The secret seed must stay secret. Anyone with it can sign as you.
 *
 * Unit tests: `stdlib/tests/ed25519_test.cpp` (RFC 8032 known-answer vectors,
 * sign/verify round-trip, tamper + wrong-key rejection); the suite runs under
 * AddressSanitizer and Valgrind on every QA-gate run.
 */
#include <string>
#include <string_view>

namespace cheatah::ed25519 {

/**
 * Generate a fresh 32-byte secret seed from the OS CSPRNG, as 64 hex chars.
 *
 * The returned seed is the private key; derive its public key with public_key(). NOT
 * reproducible (uses os-level entropy) and must be kept secret. Throws
 * `std::runtime_error` if the OS randomness source cannot be read.
 * @return a 64-char lowercase hex string (32 random bytes).
 * @complexity O(1) plus a syscall.
 * @alloc allocates the result string.
 * @test CheatahEd25519.GenerateRoundTrip
 * @crtest Ed25519CompileRun.Generate
 * @systest StdlibE2E.Ed25519
 */
std::string generate();

/**
 * Derive the 32-byte public key (hex) from a 32-byte secret seed (hex).
 *
 * Computes the Ed25519 public key per RFC 8032 (SHA-512 of the seed, clamp, scalar
 * multiply the base point). Throws `std::invalid_argument` if @p secret_hex is not
 * exactly 64 hex chars.
 * @param secret_hex the secret seed as 64 hex chars.
 * @return the public key as 64 hex chars.
 * @complexity O(1) (one fixed-base scalar multiplication).
 * @alloc allocates the result string and a temporary decoded-seed string.
 * @test CheatahEd25519.KnownVectors, CheatahEd25519.GenerateRoundTrip
 * @crtest Ed25519CompileRun.PublicKey
 * @systest StdlibE2E.Ed25519
 */
std::string public_key(std::string_view secret_hex);

/**
 * Sign @p message with the secret seed @p secret_hex; returns a 64-byte signature (hex).
 *
 * RFC 8032 Ed25519 signing (deterministic — no RNG, so the same seed and message
 * always produce the same signature). The message is raw bytes of any length. Throws
 * `std::invalid_argument` if @p secret_hex is not 64 hex chars.
 * @param secret_hex the secret seed as 64 hex chars.
 * @param message the bytes to sign.
 * @return the signature as 128 hex chars (64 bytes).
 * @complexity O(n) in the message length (two message-length SHA-512s) plus two fixed-base
 * scalar multiplications (public-key derivation and R).
 * @alloc allocates the signature string and two message-sized internal hash-input buffers
 * (plus small seed/digest temporaries).
 * @test CheatahEd25519.KnownVectors, CheatahEd25519.SignVerifyRoundTrip
 * @crtest Ed25519CompileRun.SignVerify
 * @systest StdlibE2E.Ed25519
 */
std::string sign(std::string_view secret_hex, std::string_view message);

/**
 * Verify that @p signature_hex is a valid Ed25519 signature of @p message under the
 * public key @p public_hex. Returns true iff valid.
 *
 * Touches only public data, so it is safe to run on an untrusted host. Returns false
 * (never throws) for a malformed public key/signature or a hex-decoding failure, so a
 * caller can treat any non-true result as "reject".
 * @param public_hex the public key as 64 hex chars.
 * @param message the signed bytes.
 * @param signature_hex the signature as 128 hex chars.
 * @return true iff the signature verifies.
 * @complexity O(n) in the message length plus two scalar multiplications.
 * @alloc allocates internal buffers (the decoded key/signature strings and a message-sized
 * hash-input buffer).
 * @test CheatahEd25519.KnownVectors, CheatahEd25519.RejectsTamperAndWrongKey
 * @crtest Ed25519CompileRun.SignVerify
 * @systest StdlibE2E.Ed25519
 */
bool verify(std::string_view public_hex, std::string_view message, std::string_view signature_hex);

} // namespace cheatah::ed25519
