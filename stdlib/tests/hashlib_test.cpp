#include "hashlib.hpp"

#include <string>

#include <gtest/gtest.h>

namespace hl = cheatah::hashlib;

TEST(CheatahHashlib, KnownVectors) {
    // Standard NIST/FIPS-180 SHA-256 test vectors.
    EXPECT_EQ(hl::sha256(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hl::sha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56 bytes — crosses a second 64-byte block (length-padding edge case).
    EXPECT_EQ(hl::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(CheatahHashlib, DigestShape) {
    const std::string h = hl::sha256("cheatah");
    EXPECT_EQ(h.size(), 64u);  // 64 lowercase hex chars
    for (char c : h) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << "non-hex char: " << c;
    }
}

TEST(CheatahHashlib, EmbeddedNulIsHashed) {
    // std::string_view carries the length, so an embedded NUL is part of the input.
    const std::string with_nul("a\0b", 3);
    EXPECT_NE(hl::sha256(with_nul), hl::sha256("a"));
    EXPECT_EQ(hl::sha256(with_nul).size(), 64u);
}

TEST(CheatahHashlib, Sha512KnownVectors) {
    // Standard NIST/FIPS-180 SHA-512 test vectors.
    EXPECT_EQ(hl::sha512(""),
              "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
              "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    EXPECT_EQ(hl::sha512("abc"),
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    // 112 bytes — crosses a second 128-byte block (length-padding edge case).
    EXPECT_EQ(hl::sha512("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                         "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
              "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
              "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

TEST(CheatahHashlib, Sha512DigestShape) {
    const std::string h = hl::sha512("cheatah");
    EXPECT_EQ(h.size(), 128u);  // 128 lowercase hex chars
    for (char c : h)
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << "non-hex char: " << c;
}

TEST(CheatahHashlib, RawDigestMatchesHex) {
    // The raw digest is exactly the bytes the hex form spells (Python digest()/hexdigest()).
    static const char hexd[] = "0123456789abcdef";
    auto to_hex = [](const std::string& raw) {
        std::string out;
        for (unsigned char b : raw) { out.push_back(hexd[b >> 4]); out.push_back(hexd[b & 0xF]); }
        return out;
    };
    EXPECT_EQ(hl::sha256_digest("abc").size(), 32u);
    EXPECT_EQ(hl::sha512_digest("abc").size(), 64u);
    EXPECT_EQ(to_hex(hl::sha256_digest("abc")), hl::sha256("abc"));
    EXPECT_EQ(to_hex(hl::sha512_digest("abc")), hl::sha512("abc"));
    EXPECT_EQ(to_hex(hl::sha256_digest("")), hl::sha256(""));
    EXPECT_EQ(to_hex(hl::sha512_digest("")), hl::sha512(""));
}

// ---- HMAC + HKDF (added with the TLS crypto work) ----------------------------------

// RFC 4231 test case 2: HMAC-SHA-256("Jefe", "what do ya want for nothing?").
TEST(CheatahHashlib, HmacSha256) {
    const std::string mac = cheatah::hashlib::hmac_sha256("Jefe", "what do ya want for nothing?");
    std::string hex;
    static constexpr char kHex[] = "0123456789abcdef";
    for (const char ch : mac) {
        hex.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        hex.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    EXPECT_EQ(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

namespace {
// Lowercase-hex of a raw byte string (for comparing raw MACs to published hex vectors).
std::string to_hex(const std::string& raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (const char ch : raw) {
        out.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    return out;
}
}  // namespace

// RFC 4231 HMAC-SHA-512 known-answer vectors (test cases 1-7), checked byte-for-byte against the
// published hex digests.
TEST(CheatahHashlib, HmacSha512) {
    // Case 1: key = 0x0b*20, data = "Hi There".
    EXPECT_EQ(to_hex(hl::hmac_sha512(std::string(20, '\x0b'), "Hi There")),
              "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
              "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    // Case 2: key = "Jefe", data = "what do ya want for nothing?".
    EXPECT_EQ(to_hex(hl::hmac_sha512("Jefe", "what do ya want for nothing?")),
              "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
              "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    // Case 3: key = 0xaa*20, data = 0xdd*50.
    EXPECT_EQ(to_hex(hl::hmac_sha512(std::string(20, '\xaa'), std::string(50, '\xdd'))),
              "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
              "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb");
    // Case 4: key = 0x01..0x19 (25 bytes), data = 0xcd*50.
    std::string key4;
    for (int i = 1; i <= 25; ++i) key4.push_back(static_cast<char>(i));
    EXPECT_EQ(to_hex(hl::hmac_sha512(key4, std::string(50, '\xcd'))),
              "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
              "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd");
    // Case 6: key = 0xaa*131, data = "Test Using Larger Than Block-Size Key - Hash Key First".
    EXPECT_EQ(to_hex(hl::hmac_sha512(std::string(131, '\xaa'),
                                     "Test Using Larger Than Block-Size Key - Hash Key First")),
              "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
              "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598");
    // Case 7: key = 0xaa*131, longer data.
    EXPECT_EQ(to_hex(hl::hmac_sha512(
                  std::string(131, '\xaa'),
                  "This is a test using a larger than block-size key and a larger than block-size "
                  "data. The key needs to be hashed before being used by the HMAC algorithm.")),
              "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
              "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58");
}

// RFC 4648 section 10 base64 ENCODE vectors, byte-for-byte.
TEST(CheatahHashlib, Base64KnownVectors) {
    EXPECT_EQ(hl::base64_encode(""), "");
    EXPECT_EQ(hl::base64_encode("f"), "Zg==");
    EXPECT_EQ(hl::base64_encode("fo"), "Zm8=");
    EXPECT_EQ(hl::base64_encode("foo"), "Zm9v");
    EXPECT_EQ(hl::base64_encode("foob"), "Zm9vYg==");
    EXPECT_EQ(hl::base64_encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(hl::base64_encode("foobar"), "Zm9vYmFy");
    // DECODE is the exact inverse of each vector.
    EXPECT_EQ(hl::base64_decode(""), "");
    EXPECT_EQ(hl::base64_decode("Zg=="), "f");
    EXPECT_EQ(hl::base64_decode("Zm8="), "fo");
    EXPECT_EQ(hl::base64_decode("Zm9v"), "foo");
    EXPECT_EQ(hl::base64_decode("Zm9vYg=="), "foob");
    EXPECT_EQ(hl::base64_decode("Zm9vYmE="), "fooba");
    EXPECT_EQ(hl::base64_decode("Zm9vYmFy"), "foobar");
}

TEST(CheatahHashlib, Base64RoundTrip) {
    // Every one of the 256 byte values survives encode->decode unchanged.
    std::string all;
    for (int i = 0; i < 256; ++i) all.push_back(static_cast<char>(i));
    EXPECT_EQ(hl::base64_decode(hl::base64_encode(all)), all);
    // Embedded NULs are preserved (length-carrying string_view).
    const std::string with_nul("a\0b\0\0c", 6);
    EXPECT_EQ(hl::base64_decode(hl::base64_encode(with_nul)), with_nul);
    // A known non-text vector: the 3 bytes 0x14 0xfb 0x9c encode to "FPuc".
    const std::string raw3("\x14\xfb\x9c", 3);
    EXPECT_EQ(hl::base64_encode(raw3), "FPuc");
    EXPECT_EQ(hl::base64_decode("FPuc"), raw3);
    // Decode tolerates embedded whitespace/newlines (PEM-style wrapping).
    EXPECT_EQ(hl::base64_decode("Zm9v\r\nYmFy"), "foobar");
}

// RFC 5869 test case 1 (SHA-256): extract + expand to 42 bytes.
TEST(CheatahHashlib, HkdfRfc5869Case1) {
    const std::string ikm(22, '\x0b');
    const std::string salt = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                              0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    std::string info;
    for (int i = 0; i < 10; ++i) info.push_back(static_cast<char>(0xf0 + i));
    const std::string prk = cheatah::hashlib::hkdf_extract(salt, ikm);
    const std::string okm = cheatah::hashlib::hkdf_expand(prk, info, 42);
    std::string hex;
    static constexpr char kHex[] = "0123456789abcdef";
    for (const char ch : okm) {
        hex.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        hex.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    EXPECT_EQ(hex,
              "3cb25f25faacd57a90434f64d0362f2a"
              "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
              "34007208d5b887185865");
}

// The RFC bound: zero or oversized lengths are rejected.
TEST(CheatahHashlib, HkdfBounds) {
    EXPECT_EQ(cheatah::hashlib::hkdf_expand("prk", "info", 0), "");
    EXPECT_EQ(cheatah::hashlib::hkdf_expand("prk", "info", 255 * 32 + 1), "");
}
