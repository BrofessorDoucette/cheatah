// Platform-sensitive crypto validation. These are the checks most likely to expose an
// architecture / OS / compiler-specific bug, gathered into one self-contained, self-reporting
// test so the SAME correctness bar can be confirmed on each target (x86-64, ARM/Apple Silicon,
// …) simply by running the suite there. It prints the detected arch/OS and whether the AES-GCM
// hardware path is active, then:
//   • cross-checks the hardware path against the portable scalar reference, byte-for-byte,
//     across every block-boundary size with random keys/nonces/AAD/plaintext (the check that
//     a NEW hardware path on a new architecture must pass);
//   • round-trips AES-GCM and ChaCha20-Poly1305 (+ tamper rejection) over the same corpus;
//   • re-verifies canonical NIST/RFC known-answer vectors, so a miscompiled primitive on any
//     platform fails here regardless of which code path is taken.
// The random corpus is generated from a FIXED seed, so a failure reproduces identically
// everywhere. NOTE: results are only "verified" on a platform once this has actually been run
// and passed there — see docs/performance.md ("Where the cryptography is verified").
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "aead.hpp"
#include "hashlib.hpp"

namespace {
namespace ae = cheatah::aead;
namespace hl = cheatah::hashlib;

// Tiny deterministic PRNG (xorshift64) — reproducible corpus across platforms.
struct Rng {
    std::uint64_t s;
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
    std::string bytes(std::size_t n) {
        std::string out(n, '\0');
        for (char& c : out) c = static_cast<char>(next() & 0xFF);
        return out;
    }
};

std::string to_hex(const std::string& b) {
    static constexpr char H[] = "0123456789abcdef";
    std::string o;
    o.reserve(b.size() * 2);
    for (unsigned char c : b) {
        o.push_back(H[c >> 4]);
        o.push_back(H[c & 0xF]);
    }
    return o;
}

const char* arch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86-64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}
const char* os_name() {
#if defined(__APPLE__)
    return "macOS/Apple";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "other";
#endif
}
bool is_x86() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    return true;
#else
    return false;
#endif
}

// Every size that straddles a block (16), the 4-wide (64) and 8-wide (128) group boundaries,
// plus a few larger buffers — where a CTR/GHASH tail bug would hide.
const std::vector<std::size_t> kSizes = {0,  1,  15,  16,  17,  31,  32,  63,  64,  65,
                                         80, 127, 128, 129, 200, 255, 256, 512, 1000};

}  // namespace

TEST(CryptoPlatform, Report) {
    std::printf("[crypto-platform] arch=%s os=%s | AES-GCM hardware path active: %s\n", arch(),
                os_name(), ae::crypto_hardware_active() ? "YES (AES-NI + PCLMULQDQ)"
                                                        : "no (portable scalar reference)");
    std::fflush(stdout);
    // An x86 build MUST take the hardware path (else CPUID/dispatch is broken). On other
    // architectures the scalar reference runs and this is informational only.
    if (is_x86()) {
        EXPECT_TRUE(ae::crypto_hardware_active())
            << "x86 build is not using the AES-NI/PCLMULQDQ path";
    }
    SUCCEED();
}

// The key cross-architecture check: the hardware path and the portable scalar reference must
// agree byte-for-byte, and both must round-trip, over the whole size corpus with random inputs.
TEST(CryptoPlatform, AesGcmHardwareEqualsPortableAndRoundTrips) {
    Rng r{0x1234567890abcdefULL};
    for (std::size_t n : kSizes) {
        for (int trial = 0; trial < 4; ++trial) {
            const std::string key = to_hex(r.bytes(16));
            const std::string nonce = to_hex(r.bytes(12));
            const std::string aad = r.bytes(r.below(40));
            const std::string pt = r.bytes(n);

            ae::set_force_portable_crypto(false);
            const std::string hw = ae::aes128gcm_encrypt(key, nonce, aad, pt);
            ae::set_force_portable_crypto(true);
            const std::string sw = ae::aes128gcm_encrypt(key, nonce, aad, pt);
            ae::set_force_portable_crypto(false);

            ASSERT_EQ(hw.size(), pt.size() + 16) << "arch=" << arch() << " size=" << n;
            ASSERT_EQ(hw, sw) << "hardware != portable, arch=" << arch() << " size=" << n;
            EXPECT_EQ(ae::aes128gcm_decrypt(key, nonce, aad, hw), pt);  // hardware decrypt
            ae::set_force_portable_crypto(true);
            EXPECT_EQ(ae::aes128gcm_decrypt(key, nonce, aad, hw), pt);  // portable decrypt
            ae::set_force_portable_crypto(false);
        }
    }
}

TEST(CryptoPlatform, ChaCha20Poly1305RoundTripAndTamper) {
    Rng r{0xfeedfacecafebeefULL};
    for (std::size_t n : kSizes) {
        const std::string key = to_hex(r.bytes(32));
        const std::string nonce = to_hex(r.bytes(12));
        const std::string aad = r.bytes(r.below(40));
        const std::string pt = r.bytes(n);
        const std::string ct = ae::chacha20poly1305_encrypt(key, nonce, aad, pt);
        ASSERT_EQ(ct.size(), pt.size() + 16) << "arch=" << arch() << " size=" << n;
        EXPECT_EQ(ae::chacha20poly1305_decrypt(key, nonce, aad, ct), pt);
        std::string tampered = ct;
        tampered[tampered.size() - 1] = static_cast<char>(tampered.back() ^ 0x01);
        EXPECT_EQ(ae::chacha20poly1305_decrypt(key, nonce, aad, tampered), "");
    }
}

// Canonical known-answer vectors — independent of which code path is taken, so a miscompiled
// primitive on any platform is caught here too.
TEST(CryptoPlatform, KnownAnswerVectors) {
    // SHA-2 (NIST FIPS 180-4).
    EXPECT_EQ(hl::sha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hl::sha512("abc"),
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    // AES-128-GCM (NIST GCM test case 4): K, IV, AAD, P -> C || T.
    auto unhex = [](const std::string& h) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        std::string o;
        for (std::size_t i = 0; i + 1 < h.size(); i += 2)
            o.push_back(static_cast<char>((nib(h[i]) << 4) | nib(h[i + 1])));
        return o;
    };
    const std::string key = "feffe9928665731c6d6a8f9467308308";
    const std::string iv = "cafebabefacedbaddecaf888";
    const std::string aad = unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    const std::string p = unhex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2"
        "449a6b525b16aedf5aa0de657ba637b39");
    const std::string ct = ae::aes128gcm_encrypt(key, iv, aad, p);
    const std::string expected =
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5"
        "aac84aa051ba30b396a0aac973d58e091"        // ciphertext
        "5bc94fbc3221a5db94fae95ae7121a47";        // 16-byte tag
    EXPECT_EQ(to_hex(ct), expected);
    EXPECT_EQ(ae::aes128gcm_decrypt(key, iv, aad, ct), p);  // round trip
}