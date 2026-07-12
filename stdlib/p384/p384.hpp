// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file p384.hpp
 * @brief cheatah `p384` — the NIST P-384 (secp384r1) elliptic curve: ECDSA
 *        signature VERIFICATION plus the DER/SPKI parsing needed to use it with
 *        TLS certificates. From scratch, no external libraries. `import p384`
 *        to use it.
 *
 * P-384 is the curve the big CAs' issuing chains are signed with (Sectigo,
 * DigiCert, GlobalSign ECC roots), so it lets cheatah's TLS client validate the
 * `ecdsa-with-SHA384` certificate chains real CDN-fronted hosts present, and
 * P-384 leaf certificates (`ecdsa_secp384r1_sha384` CertificateVerify).
 *
 * Verify-only by design: certificate validation handles PUBLIC data, which is
 * all TLS needs from this curve (cheatah's own TLS server signs with Ed25519,
 * and JWT signing uses P-256/ES256). The field/point machinery is the shared
 * width-generic core in `p256/ec_core.hpp` — the same battle-tested Montgomery
 * arithmetic and Jacobian group law p256 runs, instantiated at 6 limbs.
 *
 * Byte conventions: scalars/coordinates are 48 big-endian bytes; an uncompressed
 * public key point is the 96 bytes X||Y; a raw ECDSA signature is the 96 bytes
 * r||s; a DER signature is SEQUENCE{ INTEGER r, INTEGER s } (the TLS/X.509 form).
 */

#include <string>
#include <string_view>

namespace cheatah::p384 {

/**
 * Verify an ECDSA/P-384 signature.
 * @param pubkey_xy the public key as 96 bytes (X||Y), big-endian.
 * @param msg_hash the message digest (e.g. 48 bytes of SHA-384); longer digests are
 *        truncated to the leftmost 384 bits (FIPS 186-4), shorter ones (e.g. SHA-256)
 *        are taken whole (X9.62 bits2int).
 * @param sig_der the signature, DER-encoded SEQUENCE{INTEGER r, INTEGER s}.
 * @return true iff the signature is valid for @p pubkey_xy over @p msg_hash.
 * @complexity two scalar multiplications.
 * @alloc small temporaries only.
 * @test CheatahP384.VerifyKnownVector
 */
[[nodiscard]] bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                              const std::string& sig_der);

/**
 * Verify an ECDSA/P-384 signature given the raw 96-byte r||s form (the JWT
 * ES384 layout).
 * @param pubkey_xy the public key (96 bytes X||Y).
 * @param msg_hash the digest (truncated to 384 bits if longer, taken whole if shorter).
 * @param sig_raw the signature as 96 bytes r||s.
 * @return true iff valid.
 * @complexity two scalar multiplications.
 * @alloc small temporaries.
 * @test CheatahP384.VerifyKnownVector
 */
[[nodiscard]] bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                              const std::string& sig_raw);

/**
 * Extract the P-384 public key point from a certificate / SubjectPublicKeyInfo
 * DER: finds the uncompressed EC point of a `secp384r1` key and returns its
 * 96 bytes (X||Y). Returns "" if the SPKI is not a P-384 uncompressed EC key.
 * @param spki_or_cert_der the certificate or SPKI DER bytes.
 * @return the 96-byte X||Y point, or "" if not a P-384 EC key.
 * @complexity O(der length).
 * @alloc the returned point.
 * @test CheatahP384.SpkiExtractsPoint
 */
[[nodiscard]] std::string spki_ec_point(std::string_view spki_or_cert_der);

}  // namespace cheatah::p384
