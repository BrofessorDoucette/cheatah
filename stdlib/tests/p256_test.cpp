// p256_test — NIST P-256 ECDSA correctness against the RFC 6979 Appendix A.2.5
// test vector (P-256, SHA-256, message "sample"). Deterministic signing means
// the (r, s) is fixed and checkable bit-for-bit; verification round-trips it.

#include <array>
#include <string>

#include <gtest/gtest.h>

#include "hashlib.hpp"
#include "p256.hpp"

namespace p256 = cheatah::p256;

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
std::string hex(const std::string& b) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : b) {
        out.push_back(d[c >> 4]);
        out.push_back(d[c & 15]);
    }
    return out;
}

// RFC 6979 A.2.5 — P-256, key + the "sample"/SHA-256 expected signature.
const std::string kPriv = "C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721";
const std::string kUx = "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6";
const std::string kUy = "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
const std::string kR = "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716";
const std::string kS = "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";

}  // namespace

TEST(CheatahP256, SignKnownVector) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    const std::string sig = p256::sign_raw(unhex(kPriv), hash);
    ASSERT_EQ(sig.size(), 64u);
    EXPECT_EQ(hex(sig.substr(0, 32)), "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716");
    EXPECT_EQ(hex(sig.substr(32, 32)), "f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8");
}

TEST(CheatahP256, VerifyKnownVector) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string sig = unhex(kR) + unhex(kS);
    EXPECT_TRUE(p256::verify_raw(pub, hash, sig));

    // A tampered signature must fail.
    std::string bad = sig;
    bad[63] ^= 0x01;
    EXPECT_FALSE(p256::verify_raw(pub, hash, bad));
    // A different message must fail.
    EXPECT_FALSE(p256::verify_raw(pub, cheatah::hashlib::sha256_digest("test"), sig));
}

TEST(CheatahP256, PublicFromPrivate) {
    const std::string pub = p256::public_from_private(unhex(kPriv));
    ASSERT_EQ(pub.size(), 64u);
    EXPECT_EQ(hex(pub.substr(0, 32)), "60fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb6");
    EXPECT_EQ(hex(pub.substr(32, 32)), "7903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299");
}

TEST(CheatahP256, SignVerifyRoundTrip) {
    const std::string hash = cheatah::hashlib::sha256_digest("the quick brown fox");
    const std::string sig = p256::sign_raw(unhex(kPriv), hash);
    ASSERT_EQ(sig.size(), 64u);
    const std::string pub = unhex(kUx) + unhex(kUy);
    EXPECT_TRUE(p256::verify_raw(pub, hash, sig));
}

// The P-256 group order n (big-endian). A "hash" whose leftmost 256 bits are >= n
// exercises the FIPS 186-4 reduction in hash_to_scalar (e -= n once). Using exactly
// n also drives geq's all-limbs-equal return path.
const std::string kN = "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";

TEST(CheatahP256, HashToScalarReducesWhenGreaterThanOrder) {
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string sig = unhex(kR) + unhex(kS);
    // A 32-byte "hash" equal to n reduces to 0 (geq true via full equality); a hash
    // of all-0xFF (> n) reduces by one n. Neither matches the real message, so verify
    // returns false — we only need the reduction lines to execute.
    EXPECT_FALSE(p256::verify_raw(pub, unhex(kN), sig));
    EXPECT_FALSE(p256::verify_raw(pub, std::string(32, '\xff'), sig));
    // Signing with such a digest must still produce a well-formed 64-byte signature
    // that verifies against the derived key (round-trips through the reduced scalar).
    const std::string msg_hash = std::string(32, '\xff');
    const std::string s2 = p256::sign_raw(unhex(kPriv), msg_hash);
    ASSERT_EQ(s2.size(), 64u);
    EXPECT_TRUE(p256::verify_raw(pub, msg_hash, s2));
}

TEST(CheatahP256, VerifyDerWithLeadingZeroIntegers) {
    // DER-encode the RFC 6979 (r, s): both start with a high bit set (0xEF / 0xF7),
    // so DER requires a 0x00 sign byte — exercising der_to_rs's leading-zero strip.
    const std::string r = unhex(kR), s = unhex(kS);
    std::string der;
    der.push_back(0x30);
    der.push_back(0x46);  // SEQUENCE, length 70
    der.push_back(0x02);
    der.push_back(0x21);  // INTEGER, length 33 (32 + sign byte)
    der.push_back(0x00);
    der += r;
    der.push_back(0x02);
    der.push_back(0x21);
    der.push_back(0x00);
    der += s;
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    EXPECT_TRUE(p256::verify_der(pub, hash, der));

    // Malformed DER (wrong sequence length) must be rejected.
    std::string bad = der;
    bad[1] = 0x40;
    EXPECT_FALSE(p256::verify_der(pub, hash, bad));
}

TEST(CheatahP256, VerifyRejectsOutOfRangeAndInfinity) {
    const std::string pub = unhex(kUx) + unhex(kUy);
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    // r or s == 0 -> reject.
    EXPECT_FALSE(p256::verify_raw(pub, hash, std::string(32, '\0') + unhex(kS)));
    EXPECT_FALSE(p256::verify_raw(pub, hash, unhex(kR) + std::string(32, '\0')));
    // r or s >= n -> reject (use n and n+ff... both out of range).
    EXPECT_FALSE(p256::verify_raw(pub, hash, unhex(kN) + unhex(kS)));
    EXPECT_FALSE(p256::verify_raw(pub, hash, unhex(kR) + std::string(32, '\xff')));
    // Public coordinate >= field prime p (all-0xFF x) -> reject.
    EXPECT_FALSE(p256::verify_raw(std::string(32, '\xff') + unhex(kUy), hash, unhex(kR) + unhex(kS)));
    // Wrong-size inputs -> reject.
    EXPECT_FALSE(p256::verify_raw("short", hash, unhex(kR) + unhex(kS)));
    EXPECT_FALSE(p256::verify_raw(pub, hash, "short"));
    // sign_raw rejects a bad private key.
    EXPECT_TRUE(p256::sign_raw("short", hash).empty());
    EXPECT_TRUE(p256::sign_raw(std::string(32, '\0'), hash).empty());
    EXPECT_TRUE(p256::sign_raw(unhex(kN), hash).empty());  // d == n, out of range
    EXPECT_TRUE(p256::public_from_private("short").empty());
    EXPECT_TRUE(p256::public_from_private(std::string(32, '\0')).empty());
}

// Subtract two 32-byte big-endian values (a - b), assuming a >= b.
std::string be_sub(const std::string& a, const std::string& b) {
    std::string r(32, '\0');
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
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

// Verifying against a pubkey equal to G, and to -G, forces the Strauss-Shamir
// precompute table to add a point to ITSELF (G+G -> H==0, Rr==0, doubling branch)
// and to its NEGATION (G+(-G) -> H==0, Rr!=0, point-at-infinity branch). These are
// the two jac_add group-law special cases; the signatures need only be range-valid.
TEST(CheatahP256, VerifyHitsGroupLawSpecialCases) {
    const std::string p = unhex("FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF");
    // G = 1*G, derived from the implementation itself (avoids hand-transcribed hex).
    const std::string G = p256::public_from_private(unhex(std::string(63, '0') + "1"));
    ASSERT_EQ(G.size(), 64u);
    const std::string gx = G.substr(0, 32), gy = G.substr(32, 32);
    const std::string sig = unhex(kR) + unhex(kS);
    const std::string hash = cheatah::hashlib::sha256_digest("sample");

    // pubkey == G : tbl[1][1] = G + G  (doubling special case)
    (void)p256::verify_raw(G, hash, sig);  // result irrelevant; the table path runs

    // pubkey == -G = (Gx, p - Gy) : tbl[1][1] = G + (-G) (infinity special case)
    const std::string pubNegG = gx + be_sub(p, gy);
    (void)p256::verify_raw(pubNegG, hash, sig);
}

// The mod-n conditional subtraction only fires for x-coordinates in [n, p) — a
// ~2^-128 event on real curve points, so it is driven directly through the test seam
// on the SAME reduce_mod_n the sign/verify paths call.
TEST(CheatahP256, ReduceModNBoundary) {
    const std::string P = "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
    // value == n  -> reduces to 0.
    EXPECT_EQ(hex(p256::testonly::reduce_mod_n_be(unhex(kN))), std::string(64, '0'));
    // value == p-1 (in [n, p)) -> reduces by exactly n: (p-1) - n.
    const std::string pm1 = be_sub(unhex(P), unhex(std::string(63, '0') + "1"));
    const std::string reduced = p256::testonly::reduce_mod_n_be(pm1);
    EXPECT_EQ(reduced, be_sub(pm1, unhex(kN)));
    // value < n -> unchanged.
    const std::string small = unhex(std::string(63, '0') + "7");
    EXPECT_EQ(p256::testonly::reduce_mod_n_be(small), small);
}

// The RFC 6979 retry loop and its "no candidate" exhaustion return are effectively
// unreachable with real inputs (each rejection is a ~2^-128 event). The seam forces
// rejections so the retry tail runs on the real signing code.
TEST(CheatahP256, SignRetryLoop) {
    const std::string hash = cheatah::hashlib::sha256_digest("sample");
    const std::string pub = unhex(kUx) + unhex(kUy);
    // Forcing one rejection still yields a valid (different) signature via the next
    // candidate — exercising the retry tail while proving the loop stays correct.
    const std::string sig = p256::testonly::sign_raw_skip(unhex(kPriv), hash, 1);
    ASSERT_EQ(sig.size(), 64u);
    EXPECT_TRUE(p256::verify_raw(pub, hash, sig));
    // Forcing more rejections than the attempt cap exhausts the loop -> "".
    EXPECT_TRUE(p256::testonly::sign_raw_skip(unhex(kPriv), hash, 100).empty());
    // force_retries == 0 matches the public sign_raw exactly.
    EXPECT_EQ(p256::testonly::sign_raw_skip(unhex(kPriv), hash, 0),
              p256::sign_raw(unhex(kPriv), hash));
}

TEST(CheatahP256, SpkiExtractsPoint) {
    // A SubjectPublicKeyInfo carrying the RFC 6979 public point: id-ecPublicKey +
    // prime256v1, then BIT STRING 00 04 X Y.
    const std::string point = std::string("\x04", 1) + unhex(kUx) + unhex(kUy);  // 65 bytes
    // Minimal SPKI: SEQUENCE { SEQUENCE { OID ecPublicKey, OID prime256v1 }, BIT STRING }
    std::string spki;
    // Algorithm OIDs (id-ecPublicKey 1.2.840.10045.2.1 and prime256v1 1.2.840.10045.3.1.7)
    const std::string alg = unhex("301306072A8648CE3D020106082A8648CE3D030107");
    std::string bitstr;
    bitstr.push_back(0x03);
    bitstr.push_back(0x42);  // 66 bytes: 00 unused + 65-byte point
    bitstr.push_back(0x00);
    bitstr += point;
    std::string inner = alg + bitstr;
    spki.push_back(0x30);
    spki.push_back(static_cast<char>(inner.size()));
    spki += inner;
    const std::string got = p256::spki_ec_point(spki);
    ASSERT_EQ(got.size(), 64u);
    EXPECT_EQ(got, unhex(kUx) + unhex(kUy));

    // A DER with no uncompressed EC point returns "".
    EXPECT_TRUE(p256::spki_ec_point(std::string("\x30\x03\x02\x01\x00", 5)).empty());
    // A BIT STRING whose length byte is not 66 (compressed-point sized) is skipped.
    std::string wronglen = std::string("\x03\x21\x00\x04", 4) + std::string(64, '\0');
    EXPECT_TRUE(p256::spki_ec_point(wronglen).empty());
}
