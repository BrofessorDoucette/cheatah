// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file aead.hpp
 * @brief cheatah `aead` — ChaCha20-Poly1305 authenticated encryption (RFC 8439), from
 *        scratch. `import aead` to use it. The record cipher of the from-scratch cheatah
 *        TLS 1.3 client (with `x25519` and hashlib's HKDF). No OpenSSL.
 *
 * Keys are 64-char hex (32 bytes); nonces are 24-char hex (12 bytes). Messages and the
 * additional authenticated data are RAW BYTE strings (binary-safe, like socket::recv).
 * The 16-byte Poly1305 tag is APPENDED to the ciphertext; decryption verifies it in
 * constant time and returns "" on ANY mismatch — tampered data never comes back.
 */
#include <string>
#include <string_view>

namespace cheatah::aead {

/**
 * Encrypt + authenticate: ChaCha20-Poly1305(key, nonce, aad, plaintext).
 *
 * @param key_hex the 64-char hex key (32 bytes).
 * @param nonce_hex the 24-char hex nonce (12 bytes); MUST be unique per key.
 * @param aad additional authenticated data — authenticated but not encrypted ("" for none).
 * @param plaintext the raw-byte message to encrypt (binary-safe).
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.Rfc8439Encrypt
 * @crtest AeadCompileRun.RoundTrip
 * @systest StdlibE2E.Aead
 */
std::string chacha20poly1305_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                                     std::string_view aad, std::string_view plaintext);

/**
 * Verify + decrypt the inverse of chacha20poly1305_encrypt.
 *
 * @param key_hex the 64-char hex key (32 bytes) used to encrypt.
 * @param nonce_hex the 24-char hex nonce (12 bytes) used to encrypt.
 * @param aad the same additional authenticated data supplied at encryption ("" for none).
 * @param ciphertext the ciphertext with its 16-byte tag appended.
 * @return the plaintext, or "" when the tag does not verify (tampering / wrong key or
 *         nonce / malformed input) — the tag check is constant-time.
 * @complexity O(|ciphertext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.Rfc8439Decrypt
 * @crtest AeadCompileRun.RoundTrip
 * @systest StdlibE2E.Aead
 */
std::string chacha20poly1305_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                                     std::string_view aad, std::string_view ciphertext);

/**
 * Encrypt + authenticate: AES-128-GCM(key, nonce, aad, plaintext) — the other TLS 1.3 record cipher
 * (TLS_AES_128_GCM_SHA256). The nonce is used as the GCM IV (J0 = nonce || 0x00000001); the 16-byte
 * GCM tag is appended.
 *
 * @param key_hex the 32-char hex key (16 bytes, AES-128).
 * @param nonce_hex the 24-char hex nonce (12 bytes), used as the GCM IV; MUST be unique per key.
 * @param aad additional authenticated data — authenticated but not encrypted ("" for none).
 * @param plaintext the raw-byte message to encrypt (binary-safe).
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.AesGcmRoundTrip
 */
std::string aes128gcm_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view plaintext);

/**
 * Verify + decrypt the inverse of aes128gcm_encrypt.
 *
 * @param key_hex the 32-char hex key (16 bytes, AES-128) used to encrypt.
 * @param nonce_hex the 24-char hex nonce (12 bytes) used to encrypt.
 * @param aad the same additional authenticated data supplied at encryption ("" for none).
 * @param ciphertext the ciphertext with its 16-byte GCM tag appended.
 * @return the plaintext, or "" when the tag does not verify (the check is constant-time) or the input
 *         is malformed.
 * @complexity O(|ciphertext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.AesGcmRejectsTamper
 */
std::string aes128gcm_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view ciphertext);

/**
 * Encrypt + authenticate: AES-256-GCM(key, nonce, aad, plaintext) — the record cipher of TLS 1.3's
 * TLS_AES_256_GCM_SHA384 suite. The nonce is the GCM IV (J0 = nonce || 0x00000001); the 16-byte tag is
 * appended. Runs the portable scalar reference (AES-128-GCM / ChaCha20 keep the hardware fast path).
 *
 * @param key_hex the 64-char hex key (32 bytes, AES-256).
 * @param nonce_hex the 24-char hex nonce (12 bytes), used as the GCM IV; MUST be unique per key.
 * @param aad additional authenticated data — authenticated but not encrypted ("" for none).
 * @param plaintext the raw-byte message to encrypt (binary-safe).
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.Aes256GcmNistKat
 */
std::string aes256gcm_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view plaintext);

/**
 * Verify + decrypt the inverse of aes256gcm_encrypt.
 *
 * @param key_hex the 64-char hex key (32 bytes, AES-256) used to encrypt.
 * @param nonce_hex the 24-char hex nonce (12 bytes) used to encrypt.
 * @param aad the same additional authenticated data supplied at encryption ("" for none).
 * @param ciphertext the ciphertext with its 16-byte GCM tag appended.
 * @return the plaintext, or "" when the tag does not verify (constant-time check) or the input is malformed.
 * @complexity O(|ciphertext| + |aad|).
 * @alloc the returned string.
 * @test CheatahAead.Aes256GcmRejectsTamper
 */
std::string aes256gcm_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view ciphertext);

/// @cond INTERNAL — a test hook (pins the scalar reference path for cross-checking), not user API
/**
 * Force the portable (non-AES-NI) AES-128-GCM path on/off. AES-128-GCM normally uses the
 * CPU's AES-NI + PCLMULQDQ instructions when present; this hook pins the scalar reference
 * implementation instead — for deterministic cross-checking that both paths agree, and so
 * the reference stays exercised on hardware that would otherwise always take the fast path.
 * @param on true to pin the portable scalar path; false to allow AES-NI again when present.
 * @complexity O(1). @alloc none. @test CheatahAead.AesGcmPortableMatchesHardware
 */
void set_force_portable_crypto(bool on);
/// @endcond

/**
 * Whether AES-128-GCM is currently using the hardware (AES-NI + PCLMULQDQ) path: true on a
 * capable x86 CPU unless set_force_portable_crypto(true) is in effect, false otherwise (e.g.
 * on ARM, where the portable scalar reference runs). Lets a platform test report and assert
 * which implementation a given machine actually exercised.
 * @return true iff the hardware (AES-NI + PCLMULQDQ) AES-128-GCM path is currently active.
 * @complexity O(1). @alloc none. @test CryptoPlatform.Report
 */
[[nodiscard]] bool crypto_hardware_active();

} // namespace cheatah::aead
