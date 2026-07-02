#include "ed25519.hpp"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace ed = cheatah::ed25519;

namespace {
// Decode a hex string to raw bytes (test messages are given in hex so embedded NULs and
// arbitrary bytes round-trip cleanly).
std::string unhex(const std::string& h) {
    auto nib = [](char c) { return (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string o;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        o.push_back(static_cast<char>((nib(h[i]) << 4) | nib(h[i + 1])));
    return o;
}

struct Kat { const char* seed; const char* pub; const char* msg_hex; const char* sig; };
// RFC 8032 §7.1 known-answer vectors (TEST 1 empty message, TEST 2 one byte, TEST 3
// "abc"), cross-checked against OpenSSL's Ed25519.
const Kat kKats[] = {
    {"9d61b19deffebc3a06b057fd8b2bf1d8d3e3b8a5c4c8e9c3a7b6e5d4c3b2a19a",
     "a43124c1d79ede5c218a5a82f84369ea88c67875d704ad0539aed3ab66598523",
     "",
     "51aa65a70811346bb9270b06575c80e3d1ddb4f170dcd2a22aea281f008054c6"
     "99f29caee0415f7fd423c79dc8cae33a4ece28e777226605378d1f228a60fa06"},
    {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
     "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
     "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
    {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
     "616263",
     "34bf2f0eba20dfbff08e8218a18fbbf0cfd521616bbe5d781e96150cb1b48599"
     "277944b1052bcb6d88d84d5fca176ecda8b32429557009ab357c7d536dce4b00"},
};
} // namespace

TEST(CheatahEd25519, KnownVectors) {
    for (const Kat& k : kKats) {
        const std::string msg = unhex(k.msg_hex);
        EXPECT_EQ(ed::public_key(k.seed), k.pub);
        EXPECT_EQ(ed::sign(k.seed, msg), k.sig);  // signing is deterministic
        EXPECT_TRUE(ed::verify(k.pub, msg, k.sig));
    }
}

TEST(CheatahEd25519, SignVerifyRoundTrip) {
    const std::string seed = kKats[2].seed;
    const std::string pub = ed::public_key(seed);
    const std::string msg = "the quick brown fox";
    const std::string sig = ed::sign(seed, msg);
    EXPECT_TRUE(ed::verify(pub, msg, sig));
}

TEST(CheatahEd25519, RejectsTamperAndWrongKey) {
    const std::string seed = kKats[2].seed;
    const std::string pub = ed::public_key(seed);
    const std::string msg = "cheatah module integrity";
    const std::string sig = ed::sign(seed, msg);

    // Flip one character of the message — must fail.
    std::string bad_msg = msg;
    bad_msg[0] = 'C';
    EXPECT_FALSE(ed::verify(pub, bad_msg, sig));

    // Flip one byte of the signature — must fail.
    std::string bad_sig = sig;
    bad_sig[20] = (bad_sig[20] == '0') ? '1' : '0';
    EXPECT_FALSE(ed::verify(pub, msg, bad_sig));

    // A different key — must fail.
    const std::string other = ed::public_key(kKats[1].seed);
    EXPECT_FALSE(ed::verify(other, msg, sig));

    // Malformed inputs are rejected (return false), never throw.
    EXPECT_FALSE(ed::verify("zz", msg, sig));        // non-hex public key
    EXPECT_FALSE(ed::verify(pub, msg, "abcd"));      // wrong-length signature
    EXPECT_FALSE(ed::verify("00", msg, sig));        // wrong-length public key
    EXPECT_FALSE(ed::verify("abc", msg, sig));       // odd-length hex -> from_hex throws -> caught
    EXPECT_FALSE(ed::verify(pub, msg, "abcde"));     // odd-length signature hex -> caught
}

TEST(CheatahEd25519, RejectsNonCanonicalS) {
    // RFC 8032 strict verification rejects a signature whose scalar S is not reduced mod
    // the group order L (anti-malleability). Build R||S with S exactly == L (little-endian)
    // and with S all-0xFF (well above L); both must be refused.
    const std::string seed = kKats[2].seed;
    const std::string pub = ed::public_key(seed);
    const std::string msg = "non-canonical S";
    const std::string good = ed::sign(seed, msg);
    ASSERT_TRUE(ed::verify(pub, msg, good));  // the canonical signature is accepted

    const std::string R = good.substr(0, 64);            // reuse a real R half
    const std::string L_hex =                            // L, little-endian (S == L)
        "edd3f55c1a631258d69cf7a2def9de14000000000000000000000000000000" "10";
    EXPECT_FALSE(ed::verify(pub, msg, R + L_hex));
    EXPECT_FALSE(ed::verify(pub, msg, R + std::string(64, 'f')));  // S = 0xFF…FF >> L
}

TEST(CheatahEd25519, GenerateRoundTrip) {
    const std::string s1 = ed::generate();
    const std::string s2 = ed::generate();
    EXPECT_EQ(s1.size(), 64u);                 // 32-byte seed as hex
    EXPECT_NE(s1, s2);                          // fresh entropy each call
    const std::string pub = ed::public_key(s1);
    EXPECT_EQ(pub.size(), 64u);
    const std::string msg = "sign with a generated key";
    EXPECT_TRUE(ed::verify(pub, msg, ed::sign(s1, msg)));
}

TEST(CheatahEd25519, RejectsMalformedSecret) {
    EXPECT_THROW(ed::public_key("abc"), std::invalid_argument);   // odd length
    EXPECT_THROW(ed::public_key("zz"), std::invalid_argument);    // non-hex
    EXPECT_THROW(ed::public_key("00"), std::invalid_argument);    // wrong byte count
    EXPECT_THROW(ed::sign("00", "m"), std::invalid_argument);
}
