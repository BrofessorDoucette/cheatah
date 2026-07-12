// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// p384_test — NIST P-384 ECDSA verification correctness against the RFC 6979
// Appendix A.2.6 test vectors (message "sample", SHA-384 AND SHA-256 — the SHA-256
// one pins the bits2int semantics for a hash SHORTER than the 48-byte scalar).
// p384 is verify-only (TLS certificate validation), so unlike p256 there is no
// signing path to round-trip; the deterministic RFC vectors stand in for it.

#include <string>

#include <gtest/gtest.h>

#include "hashlib.hpp"
#include "p384.hpp"

namespace p384 = cheatah::p384;

namespace {

// hex (big-endian) -> raw bytes
std::string unhex(const std::string& h) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    std::string out;
    out.reserve(h.size() / 2);
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<char>((nib(h[i]) << 4) | nib(h[i + 1])));
    return out;
}

// RFC 6979 A.2.6 — the P-384 key pair and the "sample" signatures.
const std::string kUx =
    "EC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64DEF8F0EA9055866064A254515480BC13";
const std::string kUy =
    "8015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1288B231C3AE0D4FE7344FD2533264720";
// With SHA-384:
const std::string kR384 =
    "94EDBB92A5ECB8AAD4736E56C691916B3F88140666CE9FA73D64C4EA95AD133C81A648152E44ACF96E36DD1E80FABE46";
const std::string kS384 =
    "99EF4AEB15F178CEA1FE40DB2603138F130E740A19624526203B6351D0A3A94FA329C145786E679E7B82C71A38628AC8";
// With SHA-256 (a 32-byte hash < the 48-byte scalar — the whole-value bits2int case):
const std::string kR256 =
    "21B13D1E013C7FA1392D03C5F99AF8B30C570C6F98D4EA8E354B63A21D3DAA33BDE1E888E63355D92FA2B3C36D8FB2CD";
const std::string kS256 =
    "F3AA443FB107745BF4BD77CB3891674632068A10CA67E3D45DB2266FA7D1FEEBEFDC63ECCD1AC42EC0CB8668A4FA0AB0";

// The P-384 field prime p and group order n (big-endian), and the SEC 2 base point.
const std::string kP =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF";
const std::string kN =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973";
const std::string kGx =
    "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7";
const std::string kGy =
    "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F";

// Subtract two 48-byte big-endian values (a - b), assuming a >= b.
std::string be_sub(const std::string& a, const std::string& b) {
    std::string r(48, '\0');
    int borrow = 0;
    for (int i = 47; i >= 0; --i) {
        int av = static_cast<unsigned char>(a[i]);
        int bv = static_cast<unsigned char>(b[i]) + borrow;
        int d = av - bv;
        if (d < 0) {
            d += 256;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = static_cast<char>(d);
    }
    return r;
}

}  // namespace

TEST(CheatahP384, VerifyKnownVector) {
    const std::string hash = cheatah::hashlib::sha384_digest("sample");
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string sig = unhex(kR384) + unhex(kS384);
    EXPECT_TRUE(p384::verify_raw(pub, hash, sig));

    // A tampered signature must fail.
    std::string bad = sig;
    bad[95] ^= 0x01;
    EXPECT_FALSE(p384::verify_raw(pub, hash, bad));
    // A different message must fail.
    EXPECT_FALSE(p384::verify_raw(pub, cheatah::hashlib::sha384_digest("test"), sig));
}

// The RFC's SHA-256 vector: a 32-byte hash under the 48-byte curve. bits2int takes the
// WHOLE hash as the scalar (right-aligned) — left-aligning would fail every real
// ecdsa-with-SHA256 signature made by a P-384 key (openssl's default self-cert shape).
TEST(CheatahP384, VerifyKnownVectorSha256) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    const std::string pub = unhex(kUx) + unhex(kUy);
    EXPECT_TRUE(p384::verify_raw(pub, hash, unhex(kR256) + unhex(kS256)));
    // Cross-pairing hash/signature must fail.
    EXPECT_FALSE(p384::verify_raw(pub, hash, unhex(kR384) + unhex(kS384)));
}

TEST(CheatahP384, VerifyDerWithLeadingZeroIntegers) {
    // DER-encode the RFC 6979 (r, s): both start with a high bit set (0x94 / 0x99),
    // so DER requires a 0x00 sign byte — exercising der_to_rs's leading-zero strip.
    const std::string r = unhex(kR384), s = unhex(kS384);
    std::string der;
    der.push_back(0x30);
    der.push_back(0x66);  // SEQUENCE, length 102
    der.push_back(0x02);
    der.push_back(0x31);  // INTEGER, length 49 (48 + sign byte)
    der.push_back(0x00);
    der += r;
    der.push_back(0x02);
    der.push_back(0x31);
    der.push_back(0x00);
    der += s;
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string hash = cheatah::hashlib::sha384_digest("sample");
    EXPECT_TRUE(p384::verify_der(pub, hash, der));

    // Malformed DER must be rejected: wrong sequence length, wrong outer tag, a
    // long-form length, truncation, and a non-INTEGER first element.
    std::string bad = der;
    bad[1] = 0x60;
    EXPECT_FALSE(p384::verify_der(pub, hash, bad));
    bad = der;
    bad[0] = 0x31;
    EXPECT_FALSE(p384::verify_der(pub, hash, bad));
    bad = der;
    bad[1] = static_cast<char>(0x81);  // long-form length — refused (short-form only)
    EXPECT_FALSE(p384::verify_der(pub, hash, bad));
    EXPECT_FALSE(p384::verify_der(pub, hash, der.substr(0, der.size() / 2)));
    bad = der;
    bad[2] = 0x03;
    EXPECT_FALSE(p384::verify_der(pub, hash, bad));
    EXPECT_FALSE(p384::verify_der(pub, hash, ""));
}

// SECURITY (invalid-curve point validation, SP 800-56A): a public key whose coordinates
// are in range but NOT on y^2 = x^3 - 3x + b is rejected before it enters the group law.
TEST(CheatahP384, RejectsOffCurvePublicKey) {
    const std::string hash = cheatah::hashlib::sha384_digest("sample");
    const std::string sig = unhex(kR384) + unhex(kS384);
    const std::string good = unhex(kUx) + unhex(kUy);
    ASSERT_TRUE(p384::verify_raw(good, hash, sig));  // the genuine (on-curve) key verifies

    std::string off = good;
    off[95] ^= 0x01;  // flip the low bit of y: still < p, but no longer on the curve
    EXPECT_FALSE(p384::verify_raw(off, hash, sig));

    // A point with y = 0 (never on this curve) is also refused.
    std::string y_zero = good;
    for (int i = 48; i < 96; ++i) y_zero[i] = 0;
    EXPECT_FALSE(p384::verify_raw(y_zero, hash, sig));
}

TEST(CheatahP384, VerifyRejectsOutOfRangeAndWrongSizes) {
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string hash = cheatah::hashlib::sha384_digest("sample");
    // r or s == 0 -> reject.
    EXPECT_FALSE(p384::verify_raw(pub, hash, std::string(48, '\0') + unhex(kS384)));
    EXPECT_FALSE(p384::verify_raw(pub, hash, unhex(kR384) + std::string(48, '\0')));
    // r or s >= n -> reject (n itself, and all-0xFF).
    EXPECT_FALSE(p384::verify_raw(pub, hash, unhex(kN) + unhex(kS384)));
    EXPECT_FALSE(p384::verify_raw(pub, hash, unhex(kR384) + std::string(48, '\xff')));
    // Public coordinate >= field prime p -> reject.
    EXPECT_FALSE(p384::verify_raw(std::string(48, '\xff') + unhex(kUy), hash,
                                  unhex(kR384) + unhex(kS384)));
    // Wrong-size inputs -> reject (64-byte P-256 shapes included).
    EXPECT_FALSE(p384::verify_raw("short", hash, unhex(kR384) + unhex(kS384)));
    EXPECT_FALSE(p384::verify_raw(pub, hash, "short"));
    EXPECT_FALSE(p384::verify_raw(std::string(64, '\x01'), hash, unhex(kR384) + unhex(kS384)));
    EXPECT_FALSE(p384::verify_raw(pub, hash, std::string(64, '\x01')));
}

// A 48-byte "hash" >= n exercises the FIPS 186-4 reduction in hash_to_scalar for the
// 6-limb instantiation (n itself drives geq's all-limbs-equal path; all-0xFF the subtract).
TEST(CheatahP384, HashToScalarReducesWhenGreaterThanOrder) {
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string sig = unhex(kR384) + unhex(kS384);
    EXPECT_FALSE(p384::verify_raw(pub, unhex(kN), sig));
    EXPECT_FALSE(p384::verify_raw(pub, std::string(48, '\xff'), sig));
}

// Verifying against a pubkey equal to G, and to -G, forces the Strauss-Shamir
// precompute table to add a point to ITSELF (doubling branch) and to its NEGATION
// (point-at-infinity branch) — the two jac_add group-law special cases, driven on
// the 6-limb instantiation. The signatures need only be range-valid.
TEST(CheatahP384, VerifyHitsGroupLawSpecialCases) {
    const std::string sig = unhex(kR384) + unhex(kS384);
    const std::string hash = cheatah::hashlib::sha384_digest("sample");
    // pubkey == G : tbl[1][1] = G + G  (doubling special case)
    (void)p384::verify_raw(unhex(kGx) + unhex(kGy), hash, sig);
    // pubkey == -G = (Gx, p - Gy) : tbl[1][1] = G + (-G) (infinity special case)
    (void)p384::verify_raw(unhex(kGx) + be_sub(unhex(kP), unhex(kGy)), hash, sig);
}

TEST(CheatahP384, SpkiExtractsPoint) {
    // A SubjectPublicKeyInfo carrying the RFC 6979 public point: id-ecPublicKey +
    // secp384r1, then BIT STRING 00 04 X Y.
    const std::string point = std::string("\x04", 1) + unhex(kUx) + unhex(kUy);  // 97 bytes
    // Algorithm OIDs (id-ecPublicKey 1.2.840.10045.2.1 and secp384r1 1.3.132.0.34)
    const std::string alg = unhex("301006072A8648CE3D020106052B81040022");
    std::string bitstr;
    bitstr.push_back(0x03);
    bitstr.push_back(0x62);  // 98 bytes: 00 unused + 97-byte point
    bitstr.push_back(0x00);
    bitstr += point;
    std::string inner = alg + bitstr;
    std::string spki;
    spki.push_back(0x30);
    spki.push_back(static_cast<char>(0x81));  // inner is 117 bytes -> long-form length
    spki.push_back(static_cast<char>(inner.size()));
    spki += inner;
    const std::string got = p384::spki_ec_point(spki);
    ASSERT_EQ(got.size(), 96u);
    EXPECT_EQ(got, unhex(kUx) + unhex(kUy));

    // A DER with no uncompressed EC point returns "".
    EXPECT_TRUE(p384::spki_ec_point(std::string("\x30\x03\x02\x01\x00", 5)).empty());
    // A P-256-sized BIT STRING (length 66) is NOT a P-384 point — and vice versa: the
    // two extractors are length-anchored and mutually exclusive on real SPKIs.
    const std::string p256_bitstr = std::string("\x03\x42\x00\x04", 4) + std::string(64, '\x05');
    EXPECT_TRUE(p384::spki_ec_point(p256_bitstr).empty());
}
