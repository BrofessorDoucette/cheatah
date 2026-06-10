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
