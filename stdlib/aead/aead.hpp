// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file aead.hpp
 * @brief cheatah `aead` — ChaCha20-Poly1305 (RFC 8439) and AES-GCM authenticated
 *        encryption, from scratch. `import aead` to use it. The record ciphers of the
 *        from-scratch cheatah TLS 1.3 client (with `x25519` and hashlib's HKDF). No OpenSSL.
 *
 * Keys are hex (64-char / 32 bytes for ChaCha20-Poly1305 and AES-256-GCM; 32-char /
 * 16 bytes for AES-128-GCM); nonces are 24-char hex (12 bytes). Messages and the
 * additional authenticated data are RAW BYTE strings (binary-safe, like socket::recv).
 * The 16-byte tag is APPENDED to the ciphertext; decryption verifies it in
 * constant time and returns "" on ANY mismatch — tampered data never comes back.
 */
#include <cstddef>
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
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex or a
 *         message over the 64 GiB single-message cap.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string plus a temporary Poly1305 MAC-input buffer
 *        (|aad| + |plaintext| + padding).
 * @warning The nonce MUST never repeat under the same key: nonce reuse leaks the XOR of the
 *          plaintexts and lets an attacker forge tags.
 * @test CheatahAead.Rfc8439Encrypt
 * @systest TlsSys.HandshakeAgainstOpenssl
 */
std::string chacha20poly1305_encrypt(std::string_view key_hex, std::string_view nonce_hex,
                                     std::string_view aad, std::string_view plaintext);

/**
 * ChaCha20-Poly1305 encryption into a CALLER-PROVIDED buffer — the allocation-free form.
 *
 * The string-returning form above allocates its result and a temporary MAC-input buffer, which
 * makes it unusable where allocation is forbidden or merely unwelcome: signal handlers, embedded
 * targets, hot loops that own their memory, and any caller that already has the bytes in place.
 * This form allocates NOTHING — the tag is computed by streaming the AEAD's segments through
 * Poly1305 rather than concatenating them — and is therefore safe to call from such contexts.
 * Byte-for-byte identical output to the string form (asserted by an equivalence test).
 *
 * @param key the 32-byte key.
 * @param nonce the 12-byte nonce; MUST be unique per key.
 * @param aad additional authenticated data (may be null when @p aad_len is 0).
 * @param aad_len length of @p aad in bytes.
 * @param plaintext the message to encrypt (may be null when @p plaintext_len is 0).
 * @param plaintext_len length of @p plaintext in bytes.
 * @param out receives `plaintext_len + 16` bytes: the ciphertext followed by the tag. May alias
 *        @p plaintext to encrypt in place.
 * @return false on a null @p key, @p nonce or @p out, on a null @p aad / @p plaintext with a
 *         nonzero length, or on a message over the 64 GiB single-message cap; true otherwise.
 * @complexity O(|plaintext| + |aad|).
 * @alloc none.
 * @thread any thread; no shared state. Async-signal-safe (no allocation, no locks, no errno use).
 * @warning The nonce MUST never repeat under the same key: nonce reuse leaks the XOR of the
 *          plaintexts and lets an attacker forge tags.
 * @test CheatahAead.IntoFormsMatchStringForms
 */
bool chacha20poly1305_encrypt_into(const unsigned char key[32], const unsigned char nonce[12],
                                   const unsigned char* aad, std::size_t aad_len,
                                   const unsigned char* plaintext, std::size_t plaintext_len,
                                   unsigned char* out);

/**
 * Verify + decrypt into a CALLER-PROVIDED buffer — the allocation-free inverse of
 * @ref chacha20poly1305_encrypt_into. The tag is verified in constant time before any plaintext is
 * written back to the caller.
 *
 * @param key the 32-byte key.
 * @param nonce the 12-byte nonce used to encrypt.
 * @param aad the same additional authenticated data (may be null when @p aad_len is 0).
 * @param aad_len length of @p aad in bytes.
 * @param ciphertext the ciphertext WITH its trailing 16-byte tag.
 * @param ciphertext_len total length including the tag; must be >= 16.
 * @param out receives `ciphertext_len - 16` plaintext bytes. May alias @p ciphertext. May be null
 *        ONLY when the message is tag-only (`ciphertext_len == 16`), i.e. there is no plaintext to
 *        write — so authenticating an empty message needs no buffer.
 * @return false when the tag does not verify (nothing is written), or on a malformed argument;
 *         true on success.
 * @complexity O(|ciphertext| + |aad|).
 * @alloc none.
 * @thread any thread; no shared state. Async-signal-safe (no allocation, no locks, no errno use).
 * @test CheatahAead.IntoFormsMatchStringForms
 */
bool chacha20poly1305_decrypt_into(const unsigned char key[32], const unsigned char nonce[12],
                                   const unsigned char* aad, std::size_t aad_len,
                                   const unsigned char* ciphertext, std::size_t ciphertext_len,
                                   unsigned char* out);

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
 * @alloc the returned string plus a temporary Poly1305 MAC-input buffer
 *        (|aad| + |ciphertext| + padding).
 * @test CheatahAead.Rfc8439Decrypt
 * @systest TlsSys.HandshakeAgainstOpenssl
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
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex or a
 *         message over the 64 GiB single-message cap.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string.
 * @warning The nonce MUST never repeat under the same key: GCM nonce reuse leaks the XOR of
 *          the plaintexts AND the GHASH authentication key (forgeries follow).
 * @test CheatahAead.AesGcmNistCase4
 * @systest TlsSys.HandshakeAes128Gcm
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
 * @alloc the returned string (the hardware path allocates the candidate plaintext even when
 *        the tag check fails and "" is returned).
 * @test CheatahAead.AesGcmRejectsTamperAndMalformed
 */
std::string aes128gcm_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view ciphertext);

/**
 * Encrypt + authenticate: AES-256-GCM(key, nonce, aad, plaintext) — the record cipher of TLS 1.3's
 * TLS_AES_256_GCM_SHA384 suite. The nonce is the GCM IV (J0 = nonce || 0x00000001); the 16-byte tag is
 * appended. Same runtime dispatch as AES-128-GCM: the hardware path (x86 AES-NI/PCLMULQDQ or ARMv8
 * AES/PMULL) when present and self-tested, otherwise the portable scalar reference.
 *
 * @param key_hex the 64-char hex key (32 bytes, AES-256).
 * @param nonce_hex the 24-char hex nonce (12 bytes), used as the GCM IV; MUST be unique per key.
 * @param aad additional authenticated data — authenticated but not encrypted ("" for none).
 * @param plaintext the raw-byte message to encrypt (binary-safe).
 * @return ciphertext with the 16-byte tag appended, or "" on malformed key/nonce hex or a
 *         message over the 64 GiB single-message cap.
 * @complexity O(|plaintext| + |aad|).
 * @alloc the returned string.
 * @warning The nonce MUST never repeat under the same key: GCM nonce reuse leaks the XOR of
 *          the plaintexts AND the GHASH authentication key (forgeries follow).
 * @test CheatahAead.Aes256GcmNistKat
 * @systest TlsSys.HandshakeAes256GcmSha384
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
 * @alloc the returned string (the hardware path allocates the candidate plaintext even when
 *        the tag check fails and "" is returned).
 * @test CheatahAead.Aes256GcmRejectsTamper
 */
std::string aes256gcm_decrypt(std::string_view key_hex, std::string_view nonce_hex,
                              std::string_view aad, std::string_view ciphertext);

/// @cond INTERNAL
/// a test hook (pins the scalar reference path for cross-checking), not user API
/**
 * Force the portable (non-hardware) AES-GCM path on/off (both AES-128 and AES-256). AES-GCM
 * normally uses the CPU's crypto instructions when present (x86 AES-NI/PCLMULQDQ or ARMv8
 * AES/PMULL); this hook pins the scalar reference implementation instead — for deterministic
 * cross-checking that both paths agree, and so the reference stays exercised on hardware that
 * would otherwise always take the fast path.
 * @param on true to pin the portable scalar path; false to allow the hardware path again when present.
 * @complexity O(1). @alloc none.
 * @concurrency Not thread-safe: writes a plain (non-atomic) global read by every AES-GCM
 *              call — do not flip it while another thread encrypts or decrypts.
 * @test CheatahAead.AesGcmPortableMatchesHardware
 */
void set_force_portable_crypto(bool on);
/// @endcond

/**
 * Whether AES-GCM (AES-128 and AES-256) is currently using the CPU's crypto instructions: the
 * x86 AES-NI + PCLMULQDQ path, or the ARMv8 AES + PMULL path on AArch64 (e.g. Apple Silicon).
 * True on a capable CPU unless set_force_portable_crypto(true) is in effect; false where neither
 * ISA is present (the portable scalar reference then runs). Lets a platform test report and assert
 * which implementation a given machine actually exercised.
 * @return true iff a hardware AES-GCM path (x86 AES-NI/PCLMULQDQ or ARMv8 AES/PMULL) is active.
 * @complexity O(1). @alloc none after the first call (the first call may run the one-time
 * hardware known-answer self-test, which allocates short scratch strings).
 * @test CryptoPlatform.Report
 */
[[nodiscard]] bool crypto_hardware_active();

} // namespace cheatah::aead
