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
