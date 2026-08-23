// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Unit tests for tls/rsa_verify.hpp — RSA-PSS (rsa_pss_rsae_sha256) signature verification used by the
// TLS 1.3 CertificateVerify path. A FIXED vector (a 2048-bit RSA public key + a PSS-SHA256 signature
// over a known message, produced once with OpenSSL) checks the accept path; the rest check that every
// way a signature can be wrong is rejected. Complements tls_sys_test.cpp's TlsSys.HandshakeRsaCertificate
// (which covers the accept path end-to-end against a real server) with the REJECTION paths.
#include <gtest/gtest.h>

#include <string>

#include "rsa_verify.hpp"

namespace rsa = cheatah::tls::rsa;

namespace {
std::string unhex(std::string_view h) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string o;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        o.push_back(static_cast<char>((v(h[i]) << 4) | v(h[i + 1])));
    return o;
}

// The signed message, and the RSA public key (SPKI DER) + PSS-SHA256 signature over it (OpenSSL).
const std::string kMsg = "cheatah rsa_verify unit-test vector v1";
const char* const kSpkiHex =
    "30820122300d06092a864886f70d01010105000382010f003082010a0282010100eb51a46279727366ec85978ee7a2a3"
    "92105d10e045740ac257af7bcd9b36c20a9dc045dd02aabcd275d2b924eadaf00bfe58565a6630bb443f2ed0cdebd77ad"
    "a85fa665679b4ce7868945b59badf5076d74cb28ed73f59b63f4056ff898bb3f6aae1a28e445254af15239cbcfef453af"
    "7edd34daa371322e949658ff4581529bb880f7f39bbe8fd6e90b3b81ef45c8b44898ab89b66e308b951261d379623cbf9"
    "81ba3130983d4ae80ddcf980b3409e189fb3f1ee3802044bf31fb18f6dfe37ff823523be6f6af27824d49ff4949f60d1b"
    "e534b4b9671deab950693a86f6b7ad60ad30b57c83f061f561a6597ad822d1068d36d30721034fddd0ac5578210991020"
    "3010001";
const char* const kSigHex =
    "887ff2cc3ae18615c3764ffb165fc9ef726669fd870fa773bfdec5d4e86af05051024f5eda1a7d944e353f5258c195699"
    "46d3d3181820569fab9dafbd770224e602984dea308188c3f1cf7c2dcfa59acf203c708dbb5e68e4569b3afe7825c5376"
    "cadff287b9e4fdb49aafec27329bfede8aebf5117993f36e0e43bc8c52561b1b23122c1189c34f2c25210b9697f6c201b"
    "8fa46f56741bc29b05a6145b66098e3c693dca813e0498d7f6301b452e8b07c10307797e6fb8564971fa27642325f23bf"
    "01ced073761b26ecbccf26640e69b4d2efc570622074e2c0308e094c175b970c672f97a26b54da63a59f1387941147f51"
    "9d535476e911dd9685c4e40f8b3";
}  // namespace

// The genuine OpenSSL-produced signature verifies against the public key + message.
TEST(CheatahRsaVerify, AcceptsValidSignature) {
    EXPECT_TRUE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, unhex(kSigHex)));
}

// A single flipped signature byte must be rejected.
TEST(CheatahRsaVerify, RejectsTamperedSignature) {
    std::string sig = unhex(kSigHex);
    sig[100] = static_cast<char>(sig[100] ^ 0x01);
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, sig));
}

// The right signature over the WRONG message must be rejected.
TEST(CheatahRsaVerify, RejectsWrongMessage) {
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg + "!", unhex(kSigHex)));
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), "", unhex(kSigHex)));
}

// A signature whose length != the modulus length is rejected outright.
TEST(CheatahRsaVerify, RejectsWrongLengthSignature) {
    std::string sig = unhex(kSigHex);
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, sig.substr(0, sig.size() - 1)));
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, sig + std::string(1, '\0')));
}

// SECURITY: a key with a dangerous public exponent is rejected before any modexp. With e = 1
// an attacker forges a signature trivially (m = s^1 = the PSS encoding they can compute), so a
// real verifier refuses e < 3 and even e. We rewrite the exponent TLV in the SPKI (it ends with
// `02 03 01 00 01` = 65537) to e = 1 and to e = 65536 (even) and confirm both are refused.
TEST(CheatahRsaVerify, RejectsUnsafeExponent) {
    const std::string spki(kSpkiHex);
    const std::size_t at = spki.rfind("0203010001");  // the trailing exponent INTEGER
    ASSERT_NE(at, std::string::npos);

    std::string e1 = spki;
    e1.replace(at, 10, "0203000001");  // exponent value 00 00 01 -> e = 1
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(e1), kMsg, unhex(kSigHex)));

    std::string e_even = spki;
    e_even.replace(at, 10, "0203010000");  // exponent value 01 00 00 -> e = 65536 (even)
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(e_even), kMsg, unhex(kSigHex)));

    // Sanity: the untouched key (e = 65537) with its genuine signature still verifies.
    EXPECT_TRUE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, unhex(kSigHex)));
}

// A blob with no rsaEncryption SubjectPublicKeyInfo (not an RSA cert) yields no key -> reject.
TEST(CheatahRsaVerify, RejectsNonRsaCert) {
    EXPECT_FALSE(rsa::verify_pss_sha256("not a certificate", kMsg, unhex(kSigHex)));
    EXPECT_FALSE(rsa::verify_pss_sha256("", kMsg, unhex(kSigHex)));
}

// A signature representative s that is NOT strictly less than the modulus n is out of
// range and rejected (RFC 8017 §5.2.2). Using s == n exactly drives the big-integer
// comparator's equality (all-limbs-equal) return path, and s == n+1 (via the top byte)
// the greater-than path. The modulus is the 256-byte INTEGER inside kSpkiHex, just
// after the `0282010100` (INTEGER, 257 bytes incl. sign byte) tag.
TEST(CheatahRsaVerify, RejectsSignatureNotLessThanModulus) {
    const std::string spki(kSpkiHex);
    const std::string marker = "0282010100";
    const std::size_t at = spki.find(marker) + marker.size();
    const std::string mod_hex = spki.substr(at, 512);  // 256 bytes
    const std::string n = unhex(mod_hex);
    ASSERT_EQ(n.size(), 256u);
    // s == n  ->  cmp(s, n) == 0  ->  out of range.
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, n));
    // s == n with a bumped low byte (> n) -> cmp(s, n) > 0 -> out of range.
    std::string bigger = n;
    // n's least-significant byte is 0x91; incrementing keeps length and makes s > n.
    bigger.back() = static_cast<char>(static_cast<unsigned char>(bigger.back()) + 1);
    EXPECT_FALSE(rsa::verify_pss_sha256(unhex(kSpkiHex), kMsg, bigger));
}
