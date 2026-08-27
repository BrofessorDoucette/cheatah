// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file p256.hpp
 * @brief cheatah `p256` — the NIST P-256 (secp256r1) elliptic curve: ECDSA
 *        signature verification and deterministic (RFC 6979) signing, plus the
 *        DER/SPKI parsing needed to use it with TLS certificates and JWTs.
 *        From scratch, no external libraries. `import p256` to use it.
 *
 * It is the curve most of the world's TLS certificates and OAuth/JWT (ES256)
 * are signed with, so it lets cheatah's TLS client verify a real server's
 * certificate (ecdsa_secp256r1_sha256) and sign an ES256 JWT.
 *
 * Field/scalar arithmetic is Montgomery form over the two P-256 moduli (the
 * field prime p and the group order n); points use Jacobian coordinates — a
 * branchy Strauss-Shamir double chain for verification (public data) and a
 * constant-time fixed-base comb (branch-free point ops, masked table selection)
 * for the secret-scalar signing/keygen path. Signing uses an RFC 6979
 * deterministic nonce (HMAC-SHA256), so it needs no entropy source and never
 * repeats a nonce.
 *
 * Byte conventions: scalars/coordinates are 32 big-endian bytes; an uncompressed
 * public key point is the 64 bytes X||Y; a raw ECDSA signature is the 64 bytes
 * r||s (the JWT ES256 form); a DER signature is SEQUENCE{ INTEGER r, INTEGER s }
 * (the TLS/X.509 form).
 */

#include <string>
#include <string_view>

namespace cheatah::p256 {

/**
 * Verify an ECDSA/P-256 signature.
 * @param pubkey_xy the public key as 64 bytes (X||Y), big-endian.
 * @param msg_hash the message digest (e.g. 32 bytes of SHA-256); if longer than
 *        32 bytes it is truncated to the leftmost 256 bits (FIPS 186-4).
 * @param sig_der the signature, DER-encoded SEQUENCE{INTEGER r, INTEGER s}.
 * @return true iff the signature is valid for @p pubkey_xy over @p msg_hash.
 * @complexity O(1) — two scalar multiplications, computed as one Strauss-Shamir double chain.
 * @alloc a temporary raw r||s signature string.
 * @test CheatahP256.VerifyDerWithLeadingZeroIntegers, CheatahP256.RsToDerRoundTripsAndRejects
 */
[[nodiscard]] bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                              const std::string& sig_der);

/**
 * Verify an ECDSA/P-256 signature given the raw 64-byte r||s form (the JWT
 * ES256 layout).
 * @param pubkey_xy the public key (64 bytes X||Y).
 * @param msg_hash the digest (truncated to 256 bits if longer).
 * @param sig_raw the signature as 64 bytes r||s.
 * @return true iff valid.
 * @complexity O(1) — two scalar multiplications, computed as one Strauss-Shamir double chain.
 * @alloc none.
 * @test CheatahP256.VerifyKnownVector
 */
[[nodiscard]] bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                              const std::string& sig_raw);

/**
 * Deterministic ECDSA/P-256 signing (RFC 6979 nonce, HMAC-SHA256). Returns the
 * raw 64-byte r||s signature — the form an ES256 JWT carries.
 * @param privkey the private scalar d as 32 big-endian bytes.
 * @param msg_hash the digest to sign (truncated to 256 bits if longer).
 * @return the signature as 64 bytes r||s, or "" if @p privkey is not a valid scalar (wrong
 *         length, zero, or >= the group order n), or if all 64 RFC 6979 nonce candidates fail.
 * @complexity O(1) — one fixed-base scalar multiplication per RFC 6979 candidate (almost always one).
 * @alloc the returned signature plus RFC 6979 HMAC scratch strings.
 * @test CheatahP256.SignKnownVector
 */
[[nodiscard]] std::string sign_raw(const std::string& privkey, const std::string& msg_hash);

/**
 * Encode a raw 64-byte r||s signature as the DER `SEQUENCE{INTEGER r, INTEGER s}`
 * that TLS CertificateVerify and X.509 carry — the inverse of the parse inside
 * @ref verify_der, with minimal-form integers (stripped leading zeros, 0x00 sign
 * byte when the top bit is set). Composes with @ref sign_raw to produce the wire
 * form a TLS 1.3 server sends for `ecdsa_secp256r1_sha256`.
 * @param sig_raw the 64-byte r||s signature.
 * @return the DER bytes, or "" if @p sig_raw has the wrong length or a zero integer.
 * @complexity O(1).
 * @alloc the returned string plus the two integer temporaries.
 * @test CheatahP256.RsToDerRoundTripsAndRejects
 */
[[nodiscard]] std::string rs_to_der(const std::string& sig_raw);

/**
 * Derive the public key point from a private scalar: Q = d·G.
 * @param privkey the private scalar d as 32 big-endian bytes.
 * @return the public key as 64 bytes (X||Y), or "" if d is out of range.
 * @complexity O(1) — one fixed-base scalar multiplication.
 * @alloc the returned point.
 * @test CheatahP256.PublicFromPrivate
 */
[[nodiscard]] std::string public_from_private(const std::string& privkey);

/**
 * Extract the P-256 public key point from a certificate / SubjectPublicKeyInfo
 * DER: finds the uncompressed EC point BIT STRING (`03 42 00 04 X Y`, the 66-byte
 * length only a P-256 point has) and returns its 64 bytes (X||Y); the
 * AlgorithmIdentifier OIDs are not inspected. Returns "" if no such point is present.
 * @param spki_or_cert_der the certificate or SPKI DER bytes.
 * @return the 64-byte X||Y point, or "" if not a P-256 EC key.
 * @complexity O(der length).
 * @alloc the returned point.
 * @test CheatahP256.SpkiExtractsPoint
 */
[[nodiscard]] std::string spki_ec_point(std::string_view spki_or_cert_der);

#ifdef CHEATAH_P256_TESTING
namespace testonly {
/**
 * TEST-ONLY seam. Reduce a 32-byte big-endian scalar modulo the group order n
 * using the exact `reduce_mod_n` the signing/verification paths use. Exists so the
 * conditional-subtraction branch (a value in [n, p), a ~2^-128 event on real curve
 * x-coordinates) can be exercised on the real implementation without weakening it.
 * Not part of the public P-256 API.
 */
[[nodiscard]] std::string reduce_mod_n_be(const std::string& be32);

/**
 * TEST-ONLY seam. Deterministic sign that first rejects `force_retries` valid RFC
 * 6979 nonce candidates, exercising the retry loop (and, when force_retries exceeds
 * the attempt cap, the "exhausted" empty return). With force_retries == 0 it is
 * identical to sign_raw. Not part of the public P-256 API.
 */
[[nodiscard]] std::string sign_raw_skip(const std::string& privkey,
                                        const std::string& msg_hash, int force_retries);

/**
 * TEST-ONLY seam. Differentially validate the constant-time point arithmetic used by the secret-
 * scalar signing/keygen path (jac_add_ct / jac_double_ct) against the branchy reference ops across
 * the general case and every special case (a==b, a==-b, infinity operands). Returns true iff they
 * agree everywhere. Not part of the public P-256 API.
 */
[[nodiscard]] bool ct_point_selfcheck();
}  // namespace testonly
#endif  // CHEATAH_P256_TESTING

}  // namespace cheatah::p256
