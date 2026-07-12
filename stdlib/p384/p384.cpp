// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// p384.cpp — NIST P-384 (secp384r1) ECDSA verification, from scratch. See p384.hpp.
//
// All the arithmetic is the width-generic Weierstrass core shared with p256
// (p256/ec_core.hpp), instantiated at 6 limbs; this file supplies only the P-384
// curve constants (SEC 2 / FIPS 186-4 — a typo here is caught by the base-point
// on-curve unit test and the RFC 6979 known-answer vectors) and the SPKI point
// extraction. Verify-only: TLS certificate validation never signs with P-384.

#include "p384.hpp"

#include <array>
#include <cstddef>

#include "ec_core.hpp"

namespace cheatah::p384 {

namespace {

namespace ec = cheatah::ec;

// ---- P-384 curve traits (normal form, little-endian limbs) -----------------
// p = 2^384 - 2^128 - 2^96 + 2^32 - 1
struct P384Curve {
    static constexpr std::size_t kLimbs = 6;
    static constexpr std::array<ec::u64, 6> P = {0x00000000FFFFFFFFull, 0xFFFFFFFF00000000ull,
                                                 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull,
                                                 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
    static constexpr std::array<ec::u64, 6> N = {0xECEC196ACCC52973ull, 0x581A0DB248B0A77Aull,
                                                 0xC7634D81F4372DDFull, 0xFFFFFFFFFFFFFFFFull,
                                                 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
    static constexpr std::array<ec::u64, 6> B = {0x2A85C8EDD3EC2AEFull, 0xC656398D8A2ED19Dull,
                                                 0x0314088F5013875Aull, 0x181D9C6EFE814112ull,
                                                 0x988E056BE3F82D19ull, 0xB3312FA7E23EE7E4ull};
    static constexpr std::array<ec::u64, 6> GX = {0x3A545E3872760AB7ull, 0x5502F25DBF55296Cull,
                                                  0x59F741E082542A38ull, 0x6E1D3B628BA79B98ull,
                                                  0x8EB1C71EF320AD74ull, 0xAA87CA22BE8B0537ull};
    static constexpr std::array<ec::u64, 6> GY = {0x7A431D7C90EA0E5Full, 0x0A60B1CE1D7E819Dull,
                                                  0xE9DA3113B5F0B8C0ull, 0xF8F41DBD289A147Cull,
                                                  0x5D9E98BF9292DC29ull, 0x3617DE4A96262C6Full};
};
static_assert(ec::WeierstrassCurve<P384Curve>);

}  // namespace

bool verify_raw(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_raw) {
    return ec::verify_raw<P384Curve>(pubkey_xy, msg_hash, sig_raw);
}

bool verify_der(const std::string& pubkey_xy, const std::string& msg_hash,
                const std::string& sig_der) {
    return ec::verify_der<P384Curve>(pubkey_xy, msg_hash, sig_der);
}

std::string spki_ec_point(std::string_view der) {
    // Find the uncompressed-point marker: BIT STRING (03) <len> 00 04 <X(48)><Y(48)>.
    // The OID id-ecPublicKey + secp384r1 precedes it; we anchor on the 0x04 point and
    // the P-384 BIT STRING length 98 (0x62) — mutually exclusive with p256's 66.
    const unsigned char* p = (const unsigned char*)der.data();
    const std::size_t n = der.size();
    for (std::size_t i = 0; i + 2 + 97 <= n; ++i) {
        // BIT STRING tag, then a length, then 00 (unused bits), then 04 (uncompressed)
        if (p[i] == 0x03 && p[i + 2] == 0x00 && p[i + 3] == 0x04) {
            const std::size_t len = p[i + 1];
            if (len == 98 && i + 4 + 96 <= n) return std::string((const char*)p + i + 4, 96);
        }
    }
    return std::string();
}

}  // namespace cheatah::p384
