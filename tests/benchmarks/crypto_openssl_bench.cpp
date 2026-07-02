// crypto_openssl_bench — cheatah's crypto primitives vs OpenSSL (libcrypto), the de-facto
// reference implementation. Digests, HMAC, and the two TLS 1.3 AEADs are timed on the same
// 4 KiB payload (a typical TLS record) so the rows are directly comparable. The OpenSSL
// comparison rows compile only when libcrypto headers are present (CHEATAH_HAVE_OPENSSL,
// set by the benchmark CMake when find_package(OpenSSL) succeeds); otherwise only the
// cheatah rows build, so the benchmark never becomes a hard dependency.
//
// Run with the release preset:
//   cmake --build --preset release-benchmarks
//   ./build/release/bin/cheatah_benchmarks --benchmark_filter='Crypto'
#include <benchmark/benchmark.h>

#include <string>

#include "aead.hpp"
#include "hashlib.hpp"

#ifdef CHEATAH_HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

namespace {

std::string fill(std::size_t n, unsigned seed) {
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) s.push_back(static_cast<char>((i * 131u + seed) & 0xFF));
    return s;
}

const std::string kData = fill(4096, 7);                      // 4 KiB payload
const std::string kKey = fill(32, 19);                        // raw HMAC key
const std::string kAad = "x-record-header";                   // associated data

// AEAD takes HEX key/nonce. 32-byte key (ChaCha20 / two AES-128 keys), 16-byte AES-128 key,
// 12-byte nonce. The matching RAW bytes feed OpenSSL's EVP API.
std::string to_hex(const std::string& raw) {
    static constexpr char H[] = "0123456789abcdef";
    std::string o;
    o.reserve(raw.size() * 2);
    for (unsigned char c : raw) { o.push_back(H[c >> 4]); o.push_back(H[c & 0xF]); }
    return o;
}
const std::string kKey32 = fill(32, 23);
const std::string kKey16 = fill(16, 29);
const std::string kNonce = fill(12, 31);
const std::string kKey32Hex = to_hex(kKey32);
const std::string kKey16Hex = to_hex(kKey16);
const std::string kNonceHex = to_hex(kNonce);

}  // namespace

// ---------------- SHA-256 ----------------
static void BM_CryptoSha256_Cheatah(benchmark::State& s) {
    for (auto _ : s) benchmark::DoNotOptimize(cheatah::hashlib::sha256_digest(kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoSha256_Cheatah);

// ---------------- SHA-512 ----------------
static void BM_CryptoSha512_Cheatah(benchmark::State& s) {
    for (auto _ : s) benchmark::DoNotOptimize(cheatah::hashlib::sha512_digest(kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoSha512_Cheatah);

// ---------------- HMAC-SHA256 ----------------
static void BM_CryptoHmacSha256_Cheatah(benchmark::State& s) {
    for (auto _ : s) benchmark::DoNotOptimize(cheatah::hashlib::hmac_sha256(kKey, kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoHmacSha256_Cheatah);

// ---------------- ChaCha20-Poly1305 ----------------
static void BM_CryptoChaCha20Poly1305_Cheatah(benchmark::State& s) {
    for (auto _ : s)
        benchmark::DoNotOptimize(
            cheatah::aead::chacha20poly1305_encrypt(kKey32Hex, kNonceHex, kAad, kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoChaCha20Poly1305_Cheatah);

// ---------------- AES-128-GCM ----------------
static void BM_CryptoAes128Gcm_Cheatah(benchmark::State& s) {
    for (auto _ : s)
        benchmark::DoNotOptimize(
            cheatah::aead::aes128gcm_encrypt(kKey16Hex, kNonceHex, kAad, kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoAes128Gcm_Cheatah);

#ifdef CHEATAH_HAVE_OPENSSL
namespace {
// One-shot AEAD encrypt with OpenSSL's EVP interface (ciphertext || 16-byte tag).
std::string ossl_aead(const EVP_CIPHER* cipher, const std::string& key, const std::string& nonce,
                      const std::string& aad, const std::string& pt) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::string out(pt.size() + 16, '\0');
    int len = 0, total = 0;
    EVP_EncryptInit_ex(ctx, cipher, nullptr,
                       reinterpret_cast<const unsigned char*>(key.data()),
                       reinterpret_cast<const unsigned char*>(nonce.data()));
    EVP_EncryptUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(aad.data()),
                      static_cast<int>(aad.size()));
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&out[0]), &len,
                      reinterpret_cast<const unsigned char*>(pt.data()), static_cast<int>(pt.size()));
    total = len;
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&out[0]) + total, &len);
    total += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16,
                        reinterpret_cast<unsigned char*>(&out[0]) + total);
    EVP_CIPHER_CTX_free(ctx);
    return out;
}
}  // namespace

static void BM_CryptoSha256_OpenSSL(benchmark::State& s) {
    unsigned char md[32];
    unsigned int n;
    for (auto _ : s) {
        EVP_Digest(kData.data(), kData.size(), md, &n, EVP_sha256(), nullptr);
        benchmark::DoNotOptimize(md);
    }
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoSha256_OpenSSL);

static void BM_CryptoSha512_OpenSSL(benchmark::State& s) {
    unsigned char md[64];
    unsigned int n;
    for (auto _ : s) {
        EVP_Digest(kData.data(), kData.size(), md, &n, EVP_sha512(), nullptr);
        benchmark::DoNotOptimize(md);
    }
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoSha512_OpenSSL);

static void BM_CryptoHmacSha256_OpenSSL(benchmark::State& s) {
    unsigned char md[32];
    unsigned int n;
    for (auto _ : s) {
        HMAC(EVP_sha256(), kKey.data(), static_cast<int>(kKey.size()),
             reinterpret_cast<const unsigned char*>(kData.data()), kData.size(), md, &n);
        benchmark::DoNotOptimize(md);
    }
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoHmacSha256_OpenSSL);

static void BM_CryptoChaCha20Poly1305_OpenSSL(benchmark::State& s) {
    for (auto _ : s)
        benchmark::DoNotOptimize(ossl_aead(EVP_chacha20_poly1305(), kKey32, kNonce, kAad, kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoChaCha20Poly1305_OpenSSL);

static void BM_CryptoAes128Gcm_OpenSSL(benchmark::State& s) {
    for (auto _ : s)
        benchmark::DoNotOptimize(ossl_aead(EVP_aes_128_gcm(), kKey16, kNonce, kAad, kData));
    s.SetBytesProcessed(static_cast<int64_t>(s.iterations()) * kData.size());
}
BENCHMARK(BM_CryptoAes128Gcm_OpenSSL);
#endif  // CHEATAH_HAVE_OPENSSL
