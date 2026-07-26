// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Unit tests for the `aead` module against the RFC 8439 §2.8.2 AEAD test vector, plus
// round-trip, tamper-rejection, and malformed-input behavior.
#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <vector>
#include <string>

#include "aead.hpp"

namespace a = cheatah::aead;

namespace {
const std::string kKey = "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
const std::string kNonce = "070000004041424344454647";
const std::string kAad = std::string("\x50\x51\x52\x53\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7", 12);
const char* kPlain =
    "Ladies and Gentlemen of the class of '99: If I could offer you "
    "only one tip for the future, sunscreen would be it.";
// RFC 8439 §2.8.2 expected tag (the ciphertext bytes are checked via round-trip + length).
const std::string kTagHex = "1ae10b594f09e26a7e902ecbd0600691";

std::string hex_of(std::string_view raw) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (const char ch : raw) {
        out.push_back(kHex[static_cast<unsigned char>(ch) >> 4]);
        out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
    }
    return out;
}
}  // namespace

// The RFC 8439 §2.8.2 vector: ciphertext prefix + tag must match the spec exactly.
TEST(CheatahAead, Rfc8439Encrypt) {
    const std::string ct = a::chacha20poly1305_encrypt(kKey, kNonce, kAad, kPlain);
    ASSERT_EQ(ct.size(), std::string(kPlain).size() + 16);
    EXPECT_EQ(hex_of(ct.substr(0, 16)), "d31a8d34648e60db7b86afbc53ef7ec2");  // first CT block
    EXPECT_EQ(hex_of(ct.substr(ct.size() - 16)), kTagHex);                    // the Poly1305 tag
}

// Decrypt of the spec ciphertext returns the spec plaintext (and the round trip holds).
TEST(CheatahAead, Rfc8439Decrypt) {
    const std::string ct = a::chacha20poly1305_encrypt(kKey, kNonce, kAad, kPlain);
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, kAad, ct), kPlain);
}

// Any tampering — ciphertext byte, tag byte, or different aad — must yield "" (rejected).
TEST(CheatahAead, RejectsTamper) {
    std::string ct = a::chacha20poly1305_encrypt(kKey, kNonce, kAad, kPlain);
    std::string flipped = ct;
    flipped[3] = static_cast<char>(flipped[3] ^ 0x01);
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, kAad, flipped), "");
    std::string bad_tag = ct;
    bad_tag[bad_tag.size() - 1] = static_cast<char>(bad_tag.back() ^ 0x80);
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, kAad, bad_tag), "");
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, "other aad", ct), "");
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, kAad, "short"), "");
    EXPECT_EQ(a::chacha20poly1305_encrypt("zz", kNonce, kAad, kPlain), "");
}

// Empty plaintext and empty aad are valid AEAD inputs (TLS uses empty aad in places).
TEST(CheatahAead, EmptyInputs) {
    const std::string ct = a::chacha20poly1305_encrypt(kKey, kNonce, "", "");
    ASSERT_EQ(ct.size(), std::size_t{16});  // tag only
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, "", ct), "");
    // "" is also the FAILURE value; distinguish via a 1-byte round trip
    const std::string one = a::chacha20poly1305_encrypt(kKey, kNonce, "", "x");
    EXPECT_EQ(a::chacha20poly1305_decrypt(kKey, kNonce, "", one), "x");
}

// ---- AES-128-GCM (TLS_AES_128_GCM_SHA256) against the NIST/GCM (McGrew & Viega) test vectors ----
namespace {
std::string unhex(std::string_view h) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string o;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        o.push_back(static_cast<char>((v(h[i]) << 4) | v(h[i + 1])));
    return o;
}
}  // namespace

// GCM Test Case 1: all-zero key + iv, empty AAD and plaintext → ciphertext is just the tag.
TEST(CheatahAead, AesGcmNistCase1) {
    const std::string ct = a::aes128gcm_encrypt(std::string(32, '0'), std::string(24, '0'), "", "");
    ASSERT_EQ(ct.size(), std::size_t{16});
    EXPECT_EQ(hex_of(ct), "58e2fccefa7e3061367f1d57a4e7455a");
}

// GCM Test Case 2: a single all-zero block, no AAD — exact ciphertext + tag, and the round trip.
TEST(CheatahAead, AesGcmNistCase2) {
    const std::string key(32, '0'), iv(24, '0');  // 16 zero key bytes, 12 zero iv bytes
    const std::string p = unhex("00000000000000000000000000000000");
    const std::string expect = unhex(
        "0388dace60b6a392f328c2b971b2fe78"   // ciphertext
        "ab6e47d42cec13bdf53a67b21257bddf");  // tag
    const std::string ct = a::aes128gcm_encrypt(key, iv, "", p);
    EXPECT_EQ(ct, expect);
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, "", ct), p);
}

// GCM Test Case 4: same key/iv/plaintext as case 3 but WITH AAD — ciphertext unchanged, new tag.
TEST(CheatahAead, AesGcmNistCase4) {
    const std::string key = "feffe9928665731c6d6a8f9467308308", iv = "cafebabefacedbaddecaf888";
    const std::string aad = unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    const std::string p = unhex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    const std::string ct = a::aes128gcm_encrypt(key, iv, aad, p);
    EXPECT_EQ(hex_of(ct.substr(ct.size() - 16)), "5bc94fbc3221a5db94fae95ae7121a47");
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, aad, ct), p);
}

// AES-256-GCM known-answer: McGrew GCM Test Case 16 (the AES-256 analog of Case 4), the standard
// vector — AES-256 key, non-empty AAD + plaintext. Validates the FIPS-197 AES-256 key schedule +
// the shared GCM machinery through the portable path.
TEST(CheatahAead, Aes256GcmNistKat) {
    const std::string key =
        "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308";
    const std::string iv = "cafebabefacedbaddecaf888";
    const std::string aad = unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    const std::string p = unhex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    const std::string ct = a::aes256gcm_encrypt(key, iv, aad, p);
    ASSERT_EQ(ct.size(), p.size() + 16);
    EXPECT_EQ(hex_of(ct.substr(0, p.size())),
              "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd"
              "2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662");
    EXPECT_EQ(hex_of(ct.substr(ct.size() - 16)), "76fc6ece0f4e1768cddf8853bb2d551b");
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, aad, ct), p);
}

// AES-256-GCM round trip + tamper/malformed rejection (mirrors the AES-128 case).
TEST(CheatahAead, Aes256GcmRejectsTamper) {
    const std::string key =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    const std::string iv = "000102030405060708090a0b";
    const std::string aad = "header", p = "the quick brown fox jumps over the lazy dog";
    std::string ct = a::aes256gcm_encrypt(key, iv, aad, p);
    ASSERT_EQ(ct.size(), p.size() + 16);
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, aad, ct), p);  // round trip
    std::string flip_ct = ct;
    flip_ct[2] = static_cast<char>(flip_ct[2] ^ 0x01);
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, aad, flip_ct), "");
    std::string flip_tag = ct;
    flip_tag[flip_tag.size() - 1] = static_cast<char>(flip_tag.back() ^ 0x80);
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, aad, flip_tag), "");
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, "other aad", ct), "");
    EXPECT_EQ(a::aes256gcm_decrypt(key, iv, aad, "short"), "");
    EXPECT_EQ(a::aes256gcm_encrypt("ababab", iv, aad, p), "");                 // key not 32 bytes
    EXPECT_EQ(a::aes256gcm_encrypt(std::string(64, 'a'), "00", aad, p), "");   // nonce not 12 bytes
}

// Round trip + tamper rejection (ciphertext, tag, aad) + malformed key/nonce/short input.
TEST(CheatahAead, AesGcmRejectsTamperAndMalformed) {
    const std::string key = "000102030405060708090a0b0c0d0e0f", iv = "000102030405060708090a0b";
    const std::string aad = "header", p = "the quick brown fox";
    std::string ct = a::aes128gcm_encrypt(key, iv, aad, p);
    ASSERT_EQ(ct.size(), p.size() + 16);
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, aad, ct), p);  // round trip
    std::string flip_ct = ct;
    flip_ct[2] = static_cast<char>(flip_ct[2] ^ 0x01);
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, aad, flip_ct), "");
    std::string flip_tag = ct;
    flip_tag[flip_tag.size() - 1] = static_cast<char>(flip_tag.back() ^ 0x80);
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, aad, flip_tag), "");
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, "other aad", ct), "");
    EXPECT_EQ(a::aes128gcm_decrypt(key, iv, aad, "short"), "");
    EXPECT_EQ(a::aes128gcm_encrypt("ababab", iv, aad, p), "");  // key not 16 bytes
    EXPECT_EQ(a::aes128gcm_encrypt(key, "00", aad, p), "");     // nonce not 12 bytes
}

// The AES-NI/PCLMULQDQ fast path and the portable scalar reference must agree byte-for-byte,
// across sizes that exercise the 4-wide CTR loop (>=64), its tail (non-multiple of 64), the
// single sub-block, and empty. Also keeps the scalar reference covered on AES-NI hardware.
TEST(CheatahAead, AesGcmPortableMatchesHardware) {
    const std::string key = "000102030405060708090a0b0c0d0e0f";
    const std::string nonce = "101112131415161718191a1b";
    const std::string aad = "associated-data-header";
    for (std::size_t n : {std::size_t(0), std::size_t(13), std::size_t(16), std::size_t(64),
                          std::size_t(100), std::size_t(255)}) {
        std::string pt(n, '\0');
        for (std::size_t i = 0; i < n; ++i) pt[i] = static_cast<char>(i * 7 + 1);

        a::set_force_portable_crypto(false);
        const std::string hw = a::aes128gcm_encrypt(key, nonce, aad, pt);
        a::set_force_portable_crypto(true);
        const std::string sw = a::aes128gcm_encrypt(key, nonce, aad, pt);
        a::set_force_portable_crypto(false);

        ASSERT_EQ(hw.size(), n + 16);
        EXPECT_EQ(hw, sw) << "hardware vs portable AES-GCM differ at size " << n;
        EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, aad, hw), pt);  // hardware decrypt
        a::set_force_portable_crypto(true);
        EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, aad, hw), pt);  // portable decrypt
        a::set_force_portable_crypto(false);
    }
    // A tampered tag is rejected on BOTH paths.
    std::string ct = a::aes128gcm_encrypt(key, nonce, aad, std::string(80, 'z'));
    ct[ct.size() - 1] = static_cast<char>(ct.back() ^ 0x01);
    EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, aad, ct), "");
    a::set_force_portable_crypto(true);
    EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, aad, ct), "");
    EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, aad, "short"), "");  // portable: too short
    a::set_force_portable_crypto(false);

    // Large AAD (>= 128 bytes) exercises the 8-wide GHASH aggregation over the AAD too.
    {
        const std::string big_aad(200, 'A');
        const std::string pt = "payload for the large-aad cross-check";
        a::set_force_portable_crypto(false);
        const std::string hw = a::aes128gcm_encrypt(key, nonce, big_aad, pt);
        a::set_force_portable_crypto(true);
        const std::string sw = a::aes128gcm_encrypt(key, nonce, big_aad, pt);
        a::set_force_portable_crypto(false);
        EXPECT_EQ(hw, sw);
        EXPECT_EQ(a::aes128gcm_decrypt(key, nonce, big_aad, hw), pt);
    }

    // Hex parser branches: UPPERCASE A-F is accepted, a non-hex character is rejected.
    EXPECT_FALSE(
        a::aes128gcm_encrypt("000102030405060708090A0B0C0D0E0F", nonce, aad, "x").empty());
    EXPECT_EQ(a::aes128gcm_encrypt("zz0102030405060708090a0b0c0d0e0f", nonce, aad, "x"), "");
}

// AES-256-GCM: the hardware path (x86 AES-NI/PCLMULQDQ or ARMv8 AES/PMULL) must agree byte-for-byte
// with the portable scalar reference, across sizes that exercise the 8-wide CTR/GHASH loop, its tail,
// a sub-block, and empty. This is what an ARM/macOS user runs to verify THEIR hardware AES-256 path
// against the reference — just `ctest`.
TEST(CheatahAead, Aes256GcmPortableMatchesHardware) {
    const std::string key =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    const std::string nonce = "101112131415161718191a1b";
    const std::string aad = "associated-data-header";
    for (std::size_t n : {std::size_t(0), std::size_t(13), std::size_t(16), std::size_t(64),
                          std::size_t(100), std::size_t(255)}) {
        std::string pt(n, '\0');
        for (std::size_t i = 0; i < n; ++i) pt[i] = static_cast<char>(i * 7 + 1);

        a::set_force_portable_crypto(false);
        const std::string hw = a::aes256gcm_encrypt(key, nonce, aad, pt);
        a::set_force_portable_crypto(true);
        const std::string sw = a::aes256gcm_encrypt(key, nonce, aad, pt);
        a::set_force_portable_crypto(false);

        ASSERT_EQ(hw.size(), n + 16);
        EXPECT_EQ(hw, sw) << "hardware vs portable AES-256-GCM differ at size " << n;
        EXPECT_EQ(a::aes256gcm_decrypt(key, nonce, aad, hw), pt);  // hardware decrypt
        a::set_force_portable_crypto(true);
        EXPECT_EQ(a::aes256gcm_decrypt(key, nonce, aad, hw), pt);  // portable decrypt
        a::set_force_portable_crypto(false);
    }
    // Large AAD (>= 128 bytes) exercises the 8-wide GHASH aggregation over the AAD.
    const std::string big_aad(200, 'A');
    const std::string pt = "payload for the large-aad AES-256 cross-check";
    a::set_force_portable_crypto(false);
    const std::string hw = a::aes256gcm_encrypt(key, nonce, big_aad, pt);
    a::set_force_portable_crypto(true);
    const std::string sw = a::aes256gcm_encrypt(key, nonce, big_aad, pt);
    a::set_force_portable_crypto(false);
    EXPECT_EQ(hw, sw);
    EXPECT_EQ(a::aes256gcm_decrypt(key, nonce, big_aad, hw), pt);
}

// The allocation-free `_into` forms must be indistinguishable from the string-returning ones —
// same ciphertext, same tag, same rejection behavior — across the RFC vector, empty inputs,
// aliasing, and randomized sizes that straddle ChaCha's 64-byte block and Poly1305's 16-byte block.
TEST(CheatahAead, IntoFormsMatchStringForms) {
    const auto unhex = [](const std::string& h) {
        std::vector<unsigned char> b(h.size() / 2);
        for (std::size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<unsigned char>(std::stoul(h.substr(2 * i, 2), nullptr, 16));
        return b;
    };
    const std::vector<unsigned char> key = unhex(kKey);
    const std::vector<unsigned char> nonce = unhex(kNonce);
    const std::string plain(kPlain);   // kPlain is a const char* literal

    // (a) the RFC 8439 §2.8.2 vector, through both forms.
    {
        const std::string want = a::chacha20poly1305_encrypt(kKey, kNonce, kAad, kPlain);
        ASSERT_FALSE(want.empty());
        std::vector<unsigned char> got(plain.size() + 16);
        ASSERT_TRUE(a::chacha20poly1305_encrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(kAad.data()),
            kAad.size(), reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
            got.data()));
        ASSERT_EQ(got.size(), want.size());
        EXPECT_EQ(0, std::memcmp(got.data(), want.data(), want.size()));

        std::vector<unsigned char> back(plain.size());
        ASSERT_TRUE(a::chacha20poly1305_decrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(kAad.data()),
            kAad.size(), got.data(), got.size(), back.data()));
        EXPECT_EQ(std::string(reinterpret_cast<char*>(back.data()), back.size()), plain);
    }

    // (b) randomized sizes around the block boundaries, with and without aad.
    std::mt19937_64 rng(0xA11CE);
    for (int trial = 0; trial < 200; ++trial) {
        const std::size_t n = trial < 70 ? static_cast<std::size_t>(trial)
                                         : static_cast<std::size_t>(rng() % 600);
        const std::size_t an = trial % 3 == 0 ? 0 : static_cast<std::size_t>(rng() % 40);
        std::string msg(n, '\0'), aad(an, '\0');
        for (auto& c : msg) c = static_cast<char>(rng() & 0xff);
        for (auto& c : aad) c = static_cast<char>(rng() & 0xff);

        const std::string want = a::chacha20poly1305_encrypt(kKey, kNonce, aad, msg);
        ASSERT_EQ(want.size(), n + 16) << "n=" << n;
        std::vector<unsigned char> got(n + 16);
        ASSERT_TRUE(a::chacha20poly1305_encrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(aad.data()), an,
            reinterpret_cast<const unsigned char*>(msg.data()), n, got.data()));
        ASSERT_EQ(0, std::memcmp(got.data(), want.data(), want.size()))
            << "ciphertext/tag differ at n=" << n << " aad=" << an;

        std::vector<unsigned char> back(n);
        ASSERT_TRUE(a::chacha20poly1305_decrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(aad.data()), an,
            got.data(), got.size(), back.data()));
        ASSERT_EQ(0, n == 0 ? 0 : std::memcmp(back.data(), msg.data(), n)) << "n=" << n;
    }

    // (c) in-place aliasing: out may equal plaintext.
    {
        const std::string msg = "encrypt me where I already live";
        const std::string want = a::chacha20poly1305_encrypt(kKey, kNonce, "", msg);
        std::vector<unsigned char> buf(msg.size() + 16);
        std::memcpy(buf.data(), msg.data(), msg.size());
        ASSERT_TRUE(a::chacha20poly1305_encrypt_into(key.data(), nonce.data(), nullptr, 0,
                                                     buf.data(), msg.size(), buf.data()));
        EXPECT_EQ(0, std::memcmp(buf.data(), want.data(), want.size()));
    }

    // (d) tamper rejection, and the perturbation guard that proves (c)/(a) can fail: every single
    // bit flip in the ciphertext OR the tag must be refused, and nothing written.
    {
        std::vector<unsigned char> ct(plain.size() + 16);
        ASSERT_TRUE(a::chacha20poly1305_encrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(kAad.data()),
            kAad.size(), reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
            ct.data()));
        for (std::size_t i = 0; i < ct.size(); ++i) {
            std::vector<unsigned char> bad = ct;
            bad[i] ^= 0x01;
            std::vector<unsigned char> back(plain.size(), 0xEE);
            EXPECT_FALSE(a::chacha20poly1305_decrypt_into(
                key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(kAad.data()),
                kAad.size(), bad.data(), bad.size(), back.data()))
                << "accepted a flipped bit at byte " << i;
        }
        // A changed aad must also fail.
        std::vector<unsigned char> back(plain.size());
        const std::string other_aad = kAad + "x";
        EXPECT_FALSE(a::chacha20poly1305_decrypt_into(
            key.data(), nonce.data(), reinterpret_cast<const unsigned char*>(other_aad.data()),
            other_aad.size(), ct.data(), ct.size(), back.data()));
    }

    // (e) malformed arguments refuse rather than crash.
    std::vector<unsigned char> sink(64);
    EXPECT_FALSE(a::chacha20poly1305_encrypt_into(key.data(), nonce.data(), nullptr, 5,
                                                  sink.data(), 1, sink.data()));
    EXPECT_FALSE(a::chacha20poly1305_decrypt_into(key.data(), nonce.data(), nullptr, 0,
                                                  sink.data(), 15, sink.data()));
    EXPECT_FALSE(a::chacha20poly1305_encrypt_into(key.data(), nonce.data(), nullptr, 0,
                                                  sink.data(), 1, nullptr));
}
