// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once
/**
 * @file aes_gcm_ni.hpp
 * @brief Hardware-accelerated AES-128-GCM — the SAME algorithm as the readable scalar
 *        reference in aead.cpp, expressed with the CPU's crypto instructions (AES-NI for
 *        the block cipher + CTR, PCLMULQDQ for the GHASH GF(2^128) multiply).
 *
 * The clear, portable version of AES and GHASH lives in `aead.cpp`; this header is the
 * "fast path" the public `aes128gcm_*` functions take WHEN `cheatah::aead::accel::available()`
 * reports the instructions are present (checked once via CPUID). Output is bit-identical to
 * the scalar path — both are validated against the NIST GCM vectors and cross-checked against
 * OpenSSL in the test suite — so the dispatch is a pure speed choice, never a behavior change.
 *
 * Everything is `inline` and per-function `target`-attributed, so this header compiles into a
 * translation unit built WITHOUT `-maes`/`-mpclmul` globally: only these functions use the
 * crypto ISA, and they are entered only after the runtime check.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// x86/x64 on any of the three compilers: GCC/Clang use __x86_64__/__i386__, MSVC uses
// _M_X64/_M_IX86. The crypto intrinsics (AES-NI, PCLMULQDQ, SSSE3) live in the same Intel
// headers everywhere; only CPU detection and the per-function ISA opt-in differ by compiler.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define CHEATAH_AEAD_X86 1
#include <immintrin.h>
#include <wmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>          // __cpuid — MSVC has no <cpuid.h>
// MSVC enables the x86 crypto intrinsics from the headers above with no per-function opt-in;
// the instructions only run after the runtime available() check, so no target attribute.
#define CHEATAH_TARGET(isa)
#else
#include <cpuid.h>           // __get_cpuid (GCC/Clang, incl. Apple Clang on Intel macOS)
// Let each function use the crypto ISA even though the TU isn't built with -maes/-mpclmul;
// it is only ever entered after available() confirms the CPU supports it.
#define CHEATAH_TARGET(isa) __attribute__((target(isa)))
#endif
// AArch64 (Apple Silicon, ARM Linux): the ARMv8 Cryptography Extension — AES (vaeseq/vaesmcq)
// and PMULL (vmull_p64) — reached the same way (per-function opt-in + runtime detection).
#elif defined(__aarch64__) && defined(__ARM_NEON)
#define CHEATAH_AEAD_ARM 1
#include <arm_neon.h>
#if defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif
#define CHEATAH_TARGET(isa) __attribute__((target(isa)))
#endif

namespace cheatah::aead::accel {

#if defined(CHEATAH_AEAD_X86)

/// Does the CPU advertise AES-NI + PCLMULQDQ + SSSE3? CPUID leaf 1 ECX: bit 25 = AES-NI,
/// bit 1 = PCLMULQDQ, bit 9 = SSSE3. (The public gate is available(), defined below — it also
/// runs a known-answer self-test before trusting the path.)
inline bool cpu_has_crypto() {
    unsigned ecx;
#if defined(_MSC_VER)
    int info[4];
    __cpuid(info, 1);
    ecx = static_cast<unsigned>(info[2]);
#else
    unsigned eax, ebx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
#endif
    const bool aesni = (ecx >> 25) & 1u, pclmul = (ecx >> 1) & 1u, ssse3 = (ecx >> 9) & 1u;
    return aesni && pclmul && ssse3;
}

// --- AES-128 key schedule via AES-NI (produces the same 11 round keys as the scalar code) ---
CHEATAH_TARGET("aes,sse2") inline __m128i key_assist(__m128i k, __m128i gen) {
    gen = _mm_shuffle_epi32(gen, _MM_SHUFFLE(3, 3, 3, 3));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    return _mm_xor_si128(k, gen);
}
CHEATAH_TARGET("aes,sse2") inline void expand(const unsigned char key[16], __m128i rk[11]) {
    rk[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
    rk[1] = key_assist(rk[0], _mm_aeskeygenassist_si128(rk[0], 0x01));
    rk[2] = key_assist(rk[1], _mm_aeskeygenassist_si128(rk[1], 0x02));
    rk[3] = key_assist(rk[2], _mm_aeskeygenassist_si128(rk[2], 0x04));
    rk[4] = key_assist(rk[3], _mm_aeskeygenassist_si128(rk[3], 0x08));
    rk[5] = key_assist(rk[4], _mm_aeskeygenassist_si128(rk[4], 0x10));
    rk[6] = key_assist(rk[5], _mm_aeskeygenassist_si128(rk[5], 0x20));
    rk[7] = key_assist(rk[6], _mm_aeskeygenassist_si128(rk[6], 0x40));
    rk[8] = key_assist(rk[7], _mm_aeskeygenassist_si128(rk[7], 0x80));
    rk[9] = key_assist(rk[8], _mm_aeskeygenassist_si128(rk[8], 0x1b));
    rk[10] = key_assist(rk[9], _mm_aeskeygenassist_si128(rk[9], 0x36));
}
// The second AES-256 key-assist (uses the 0xaa lane instead of 0xff; FIPS-197 §5.2 via AES-NI).
CHEATAH_TARGET("aes,sse2") inline __m128i key_assist2(__m128i k, __m128i gen) {
    gen = _mm_shuffle_epi32(gen, _MM_SHUFFLE(2, 2, 2, 2));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
    return _mm_xor_si128(k, gen);
}
// AES-256 key schedule via AES-NI: 15 round keys from the 32-byte key (Intel AES-NI whitepaper).
CHEATAH_TARGET("aes,sse2") inline void expand256(const unsigned char key[32], __m128i rk[15]) {
    rk[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
    rk[1] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key + 16));
    rk[2]  = key_assist (rk[0],  _mm_aeskeygenassist_si128(rk[1],  0x01));
    rk[3]  = key_assist2(rk[1],  _mm_aeskeygenassist_si128(rk[2],  0x00));
    rk[4]  = key_assist (rk[2],  _mm_aeskeygenassist_si128(rk[3],  0x02));
    rk[5]  = key_assist2(rk[3],  _mm_aeskeygenassist_si128(rk[4],  0x00));
    rk[6]  = key_assist (rk[4],  _mm_aeskeygenassist_si128(rk[5],  0x04));
    rk[7]  = key_assist2(rk[5],  _mm_aeskeygenassist_si128(rk[6],  0x00));
    rk[8]  = key_assist (rk[6],  _mm_aeskeygenassist_si128(rk[7],  0x08));
    rk[9]  = key_assist2(rk[7],  _mm_aeskeygenassist_si128(rk[8],  0x00));
    rk[10] = key_assist (rk[8],  _mm_aeskeygenassist_si128(rk[9],  0x10));
    rk[11] = key_assist2(rk[9],  _mm_aeskeygenassist_si128(rk[10], 0x00));
    rk[12] = key_assist (rk[10], _mm_aeskeygenassist_si128(rk[11], 0x20));
    rk[13] = key_assist2(rk[11], _mm_aeskeygenassist_si128(rk[12], 0x00));
    rk[14] = key_assist (rk[12], _mm_aeskeygenassist_si128(rk[13], 0x40));
}
// Encrypt one block with @p nr rounds (10 for AES-128, 14 for AES-256).
CHEATAH_TARGET("aes,sse2") inline __m128i enc_block(const __m128i* rk, int nr, __m128i b) {
    b = _mm_xor_si128(b, rk[0]);
    for (int i = 1; i < nr; ++i) b = _mm_aesenc_si128(b, rk[i]);
    return _mm_aesenclast_si128(b, rk[nr]);
}

// --- GHASH via PCLMULQDQ. Operands are byte-reversed (GCM is big-endian) so the carry-less
//     multiply + reduction below (Intel "Carry-Less Multiplication … GCM" whitepaper) yields
//     the GF(2^128) product with reduction polynomial x^128 + x^7 + x^2 + x + 1. ---
CHEATAH_TARGET("pclmul,ssse3,sse2") inline __m128i bswap(__m128i x) {
    const __m128i mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    return _mm_shuffle_epi8(x, mask);
}
// Accumulate the carry-less product a·b into the 256-bit (lo, hi) halves and the middle
// term — NO reduction yet, so several products can be summed before a single reduce (the
// expensive step). The middle clmuls are kept separate and folded in reduce().
CHEATAH_TARGET("pclmul,sse2") inline void clmul_acc(__m128i a, __m128i b, __m128i& lo,
                                                             __m128i& hi, __m128i& mid) {
    lo = _mm_xor_si128(lo, _mm_clmulepi64_si128(a, b, 0x00));
    hi = _mm_xor_si128(hi, _mm_clmulepi64_si128(a, b, 0x11));
    mid = _mm_xor_si128(mid, _mm_clmulepi64_si128(a, b, 0x01));
    mid = _mm_xor_si128(mid, _mm_clmulepi64_si128(a, b, 0x10));
}
// Fold the middle term, apply the reflect-shift (<<1), and reduce mod the GHASH polynomial.
// Both the shift and the reduction are linear over XOR, so applying them once to a SUM of
// products is identical to applying them per product — that is what lets GHASH aggregate.
CHEATAH_TARGET("pclmul,sse2") inline __m128i reduce(__m128i lo, __m128i hi, __m128i mid) {
    lo = _mm_xor_si128(lo, _mm_slli_si128(mid, 8));
    hi = _mm_xor_si128(hi, _mm_srli_si128(mid, 8));
    __m128i t7 = _mm_srli_epi32(lo, 31);
    __m128i t8 = _mm_srli_epi32(hi, 31);
    lo = _mm_slli_epi32(lo, 1);
    hi = _mm_slli_epi32(hi, 1);
    __m128i t9 = _mm_srli_si128(t7, 12);
    t8 = _mm_slli_si128(t8, 4);
    t7 = _mm_slli_si128(t7, 4);
    lo = _mm_or_si128(lo, t7);
    hi = _mm_or_si128(hi, t8);
    hi = _mm_or_si128(hi, t9);
    t7 = _mm_slli_epi32(lo, 31);
    t8 = _mm_slli_epi32(lo, 30);
    t9 = _mm_slli_epi32(lo, 25);
    t7 = _mm_xor_si128(t7, t8);
    t7 = _mm_xor_si128(t7, t9);
    t8 = _mm_srli_si128(t7, 4);
    t7 = _mm_slli_si128(t7, 12);
    lo = _mm_xor_si128(lo, t7);
    __m128i t2 = _mm_srli_epi32(lo, 1);
    __m128i t4 = _mm_srli_epi32(lo, 2);
    __m128i t5 = _mm_srli_epi32(lo, 7);
    t2 = _mm_xor_si128(t2, t4);
    t2 = _mm_xor_si128(t2, t5);
    t2 = _mm_xor_si128(t2, t8);
    lo = _mm_xor_si128(lo, t2);
    return _mm_xor_si128(hi, lo);
}
// Single-block GHASH multiply Y·H (the per-block / tail form).
CHEATAH_TARGET("pclmul,sse2") inline __m128i gfmul(__m128i a, __m128i b) {
    __m128i lo = _mm_setzero_si128(), hi = _mm_setzero_si128(), mid = _mm_setzero_si128();
    clmul_acc(a, b, lo, hi, mid);
    return reduce(lo, hi, mid);
}

// GHASH the zero-padded buffer into the (reversed-domain) accumulator Y. Hp holds the
// precomputed powers {H, H^2, …, H^8}. EIGHT blocks are aggregated per reduction via the
// Horner identity  Y8 = (Y^b0)·H^8 ^ b1·H^7 ^ … ^ b7·H  — only ONE reduce per 8 blocks, since
// the reduction dominates the cost. The non-multiple-of-128 tail uses the single-block form.
CHEATAH_TARGET("pclmul,ssse3,sse2") inline __m128i
ghash_buf(__m128i Y, const __m128i Hp[8], const unsigned char* p, std::size_t n) {
    std::size_t off = 0;
    for (; off + 128 <= n; off += 128) {  // 8 blocks per reduction: Y8 = Σ b_i · H^(8-i)
        __m128i b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = bswap(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p + off + 16 * i)));
        __m128i lo = _mm_setzero_si128(), hi = _mm_setzero_si128(), mid = _mm_setzero_si128();
        clmul_acc(_mm_xor_si128(Y, b[0]), Hp[7], lo, hi, mid);     // (Y ^ b0)·H^8
        for (int i = 1; i < 8; ++i) clmul_acc(b[i], Hp[7 - i], lo, hi, mid);  // b_i·H^(8-i)
        Y = reduce(lo, hi, mid);
    }
    for (; off < n; off += 16) {
        __m128i blk;
        if (n - off >= 16) {
            blk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + off));
        } else {
            unsigned char tmp[16] = {0};
            std::memcpy(tmp, p + off, n - off);
            blk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(tmp));
        }
        Y = _mm_xor_si128(Y, bswap(blk));
        Y = gfmul(Y, Hp[0]);
    }
    return Y;
}

// inc32 on the rightmost 32 bits (big-endian) of a counter held as a __m128i (byte order).
CHEATAH_TARGET("ssse3,sse2") inline __m128i ctr_inc(__m128i c) {
    alignas(16) unsigned char b[16];
    _mm_storeu_si128(reinterpret_cast<__m128i*>(b), c);
    for (int j = 15; j >= 12; --j)
        if (++b[j] != 0) break;
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
}

// Stitched CTR + GHASH: in ONE pass over `buf`, CTR-encrypt 4 blocks at a time AND fold the
// resulting ciphertext blocks into the GHASH accumulator Y — so the AES-NI and PCLMULQDQ
// pipelines run concurrently (the win OpenSSL gets from interleaving the two passes).
// `encrypt`=true reads plaintext and GHASHes the ciphertext it writes; false reads ciphertext,
// GHASHes it, and writes plaintext. GHASH is always over the CIPHERTEXT. Returns the updated
// accumulator Y (over the data only; the caller folds in AAD beforehand and the lengths after).
CHEATAH_TARGET("aes,pclmul,ssse3,sse2") inline __m128i
ctr_ghash_stitch(const __m128i* rk, int nr, const __m128i Hp[8], __m128i ctr, __m128i Y,
                 unsigned char* buf, std::size_t len, bool encrypt) {
    std::size_t off = 0;
    for (; off + 128 <= len; off += 128) {  // 8 blocks: AES-CTR + 8-way aggregated GHASH
        __m128i c[8];
        c[0] = ctr;
        for (int i = 1; i < 8; ++i) c[i] = ctr_inc(c[i - 1]);
        auto* d = reinterpret_cast<__m128i*>(buf + off);
        __m128i g[8];
        for (int i = 0; i < 8; ++i) {
            const __m128i in = _mm_loadu_si128(d + i);
            const __m128i out = _mm_xor_si128(in, enc_block(rk, nr, c[i]));
            _mm_storeu_si128(d + i, out);
            g[i] = bswap(encrypt ? out : in);
        }
        __m128i lo = _mm_setzero_si128(), hi = _mm_setzero_si128(), mid = _mm_setzero_si128();
        clmul_acc(_mm_xor_si128(Y, g[0]), Hp[7], lo, hi, mid);
        for (int i = 1; i < 8; ++i) clmul_acc(g[i], Hp[7 - i], lo, hi, mid);
        Y = reduce(lo, hi, mid);
        ctr = ctr_inc(c[7]);
    }
    for (; off + 16 <= len; off += 16) {  // full 16-byte tail blocks
        auto* d = reinterpret_cast<__m128i*>(buf + off);
        const __m128i in = _mm_loadu_si128(d);
        const __m128i out = _mm_xor_si128(in, enc_block(rk, nr, ctr));
        _mm_storeu_si128(d, out);
        Y = gfmul(_mm_xor_si128(Y, bswap(encrypt ? out : in)), Hp[0]);
        ctr = ctr_inc(ctr);
    }
    if (off < len) {  // final partial block (< 16 bytes); GHASH the zero-padded ciphertext
        const std::size_t m = len - off;
        alignas(16) unsigned char ksb[16];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(ksb), enc_block(rk, nr, ctr));
        alignas(16) unsigned char gb[16] = {0};
        for (std::size_t i = 0; i < m; ++i) {
            const unsigned char cin = buf[off + i];
            const unsigned char cout = static_cast<unsigned char>(cin ^ ksb[i]);
            gb[i] = encrypt ? cout : cin;
            buf[off + i] = cout;
        }
        Y = gfmul(_mm_xor_si128(Y, bswap(_mm_loadu_si128(reinterpret_cast<const __m128i*>(gb)))),
                  Hp[0]);
    }
    return Y;
}

// Finalize the tag from the data accumulator Y: fold the length block
// (len(AAD)_64 || len(C)_64), do the last GHASH multiply, and XOR E(J0).
CHEATAH_TARGET("aes,pclmul,ssse3,sse2") inline void
finalize_tag(const __m128i* rk, int nr, const __m128i Hp[4], __m128i J0, __m128i Y, std::size_t aadlen,
             std::size_t ctlen, unsigned char tag[16]) {
    alignas(16) unsigned char lb[16] = {0};
    const std::uint64_t abits = static_cast<std::uint64_t>(aadlen) * 8;
    const std::uint64_t cbits = static_cast<std::uint64_t>(ctlen) * 8;
    for (int i = 0; i < 8; ++i) {
        lb[7 - i] = static_cast<unsigned char>(abits >> (8 * i));
        lb[15 - i] = static_cast<unsigned char>(cbits >> (8 * i));
    }
    Y = _mm_xor_si128(Y, bswap(_mm_loadu_si128(reinterpret_cast<const __m128i*>(lb))));
    Y = gfmul(Y, Hp[0]);
    const __m128i t = _mm_xor_si128(bswap(Y), enc_block(rk, nr, J0));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(tag), t);
}

// Shared setup: round keys (10 rounds for a 16-byte key, 14 for 32), the GHASH-domain subkey powers
// Hp = {H, H^2, …, H^8} (H = E(0)), and J0. @p rk must hold up to 15 round keys. Returns the round count.
CHEATAH_TARGET("aes,pclmul,ssse3,sse2") inline int
setup(const unsigned char* key, std::size_t keylen, const unsigned char nonce[12], __m128i* rk,
      __m128i Hp[8], __m128i& J0) {
    int nr;
    if (keylen == 32) { expand256(key, rk); nr = 14; }
    else { expand(key, rk); nr = 10; }
    const __m128i H = bswap(enc_block(rk, nr, _mm_setzero_si128()));
    Hp[0] = H;
    for (int i = 1; i < 8; ++i) Hp[i] = gfmul(Hp[i - 1], H);  // H^2 … H^8
    alignas(16) unsigned char j0[16] = {0};
    std::memcpy(j0, nonce, 12);
    j0[15] = 1;
    J0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(j0));
    return nr;
}

// AES-GCM encrypt for a 16- or 32-byte key (@p keylen); returns ciphertext with the 16-byte tag appended.
CHEATAH_TARGET("aes,pclmul,ssse3,sse2") inline std::string
gcm_encrypt(const unsigned char* key, std::size_t keylen, const unsigned char nonce[12],
            std::string_view aad, std::string_view plaintext) {
    __m128i rk[15], Hp[8], J0;
    const int nr = setup(key, keylen, nonce, rk, Hp, J0);
    std::string ct(plaintext);
    // GHASH the AAD, then stitch CTR-encryption with GHASH over the ciphertext in one pass.
    __m128i Y = ghash_buf(_mm_setzero_si128(), Hp,
                          reinterpret_cast<const unsigned char*>(aad.data()), aad.size());
    Y = ctr_ghash_stitch(rk, nr, Hp, ctr_inc(J0), Y, reinterpret_cast<unsigned char*>(&ct[0]),
                         ct.size(), /*encrypt=*/true);
    unsigned char tag[16];
    finalize_tag(rk, nr, Hp, J0, Y, aad.size(), ct.size(), tag);
    ct.append(reinterpret_cast<char*>(tag), 16);
    return ct;
}

// AES-GCM decrypt for a 16- or 32-byte key; verifies the appended tag in constant time; "" on failure.
CHEATAH_TARGET("aes,pclmul,ssse3,sse2") inline std::string
gcm_decrypt(const unsigned char* key, std::size_t keylen, const unsigned char nonce[12],
            std::string_view aad, std::string_view ciphertext) {
    if (ciphertext.size() < 16) return "";
    const std::string_view ct = ciphertext.substr(0, ciphertext.size() - 16);
    const std::string_view want = ciphertext.substr(ciphertext.size() - 16);
    __m128i rk[15], Hp[8], J0;
    const int nr = setup(key, keylen, nonce, rk, Hp, J0);
    // GHASH the AAD, then stitch GHASH-over-ciphertext with CTR-decryption in one pass.
    std::string pt(ct);
    __m128i Y = ghash_buf(_mm_setzero_si128(), Hp,
                          reinterpret_cast<const unsigned char*>(aad.data()), aad.size());
    Y = ctr_ghash_stitch(rk, nr, Hp, ctr_inc(J0), Y, reinterpret_cast<unsigned char*>(&pt[0]),
                         pt.size(), /*encrypt=*/false);
    unsigned char tag[16];
    finalize_tag(rk, nr, Hp, J0, Y, aad.size(), ct.size(), tag);
    unsigned char diff = 0;  // constant-time tag compare; plaintext discarded on mismatch
    for (int i = 0; i < 16; ++i) diff |= tag[i] ^ static_cast<unsigned char>(want[i]);
    if (diff != 0) return "";
    return pt;
}

// The public accel entry points are `gcm_encrypt`/`gcm_decrypt(key, keylen, …)` above — callers pass
// keylen 16 (AES-128, TLS_AES_128_GCM_SHA256) or 32 (AES-256, TLS_AES_256_GCM_SHA384).

#ifndef CHEATAH_NO_CRYPTO_SELFTEST
// Power-on self-test: run a known-answer vector (NIST GCM AES-128 test case 4) through the
// hardware path and confirm encrypt + decrypt match before the path is ever trusted. This is
// what makes an UNTESTED SIMD path (a new architecture, a miscompile) safe to ship: if it does
// not reproduce the vector, available() returns false and the portable scalar reference runs —
// the fast path is never used to produce wrong cryptography, only disabled. ON BY DEFAULT;
// define CHEATAH_NO_CRYPTO_SELFTEST (purrc's --no-crypto-selftest) to skip it and trust CPUID.
inline bool self_test() {
    static const unsigned char key[16] = {0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
                                          0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08};
    static const unsigned char nonce[12] = {0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
                                            0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88};
    static const unsigned char pt[60] = {
        0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26,
        0x9a, 0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda, 0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31,
        0x8a, 0x72, 0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49,
        0xa6, 0xb5, 0x25, 0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39};
    static const unsigned char aad[20] = {0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
                                          0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xab, 0xad, 0xda, 0xd2};
    static const unsigned char want[76] = {  // ciphertext (60) || tag (16)
        0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4,
        0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0, 0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac,
        0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac,
        0x84, 0xaa, 0x05, 0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91,
        0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb, 0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a,
        0x47};
    // AES-256 known-answer (McGrew GCM Test Case 16): same nonce/pt/aad, 32-byte key. Gates the
    // AES-256 hardware path too — if its key schedule / 14-round enc is wrong on this CPU, available()
    // returns false and BOTH AES-128 and AES-256 fall back to the portable scalar reference.
    static const unsigned char key256[32] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08};
    static const unsigned char want256[76] = {
        0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07, 0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
        0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9, 0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
        0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d, 0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
        0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a, 0xbc, 0xc9, 0xf6, 0x62, 0x76, 0xfc, 0x6e, 0xce,
        0x0f, 0x4e, 0x17, 0x68, 0xcd, 0xdf, 0x88, 0x53, 0xbb, 0x2d, 0x55, 0x1b};
    const std::string_view aadv{reinterpret_cast<const char*>(aad), sizeof aad};
    const std::string_view ptv{reinterpret_cast<const char*>(pt), sizeof pt};
    const std::string ct = gcm_encrypt(key, 16, nonce, aadv, ptv);
    const std::string back = gcm_decrypt(key, 16, nonce, aadv, ct);
    const std::string ct256 = gcm_encrypt(key256, 32, nonce, aadv, ptv);
    const std::string back256 = gcm_decrypt(key256, 32, nonce, aadv, ct256);
    // Branchless so both arms aren't a failure-only (uncovered) path on a working machine.
    return ct.size() == sizeof want && std::memcmp(ct.data(), want, sizeof want) == 0 &&
           back.size() == sizeof pt && std::memcmp(back.data(), pt, sizeof pt) == 0 &&
           ct256.size() == sizeof want256 && std::memcmp(ct256.data(), want256, sizeof want256) == 0 &&
           back256.size() == sizeof pt && std::memcmp(back256.data(), pt, sizeof pt) == 0;
}
#endif  // CHEATAH_NO_CRYPTO_SELFTEST

/// The hardware path is used only if the CPU advertises the instructions AND (unless the
/// self-test is disabled) the path reproduces the known-answer vector — checked once, cached.
/// Otherwise the scalar reference runs.
inline bool available() {
#ifdef CHEATAH_NO_CRYPTO_SELFTEST
    static const bool ok = cpu_has_crypto();
#else
    static const bool ok = cpu_has_crypto() && self_test();
#endif
    return ok;
}

#elif defined(CHEATAH_AEAD_ARM)  // ===== AArch64: ARMv8 AES + PMULL (the same algorithm) =====

// ARMv8 Cryptography Extension present? Linux: HWCAP via getauxval. Apple Silicon: always
// (every arm64 Mac has FEAT_AES/FEAT_PMULL). Other OSes conservatively assume absent.
inline bool cpu_has_crypto() {
#if defined(__linux__)
    const unsigned long hw = getauxval(AT_HWCAP);
    return (hw & HWCAP_AES) != 0 && (hw & HWCAP_PMULL) != 0;
#elif defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

// AES-128 key schedule (FIPS-197, scalar — ARM has no key-assist instruction); identical
// round-key bytes to the portable reference, loaded into NEON registers.
// Shared AES S-box + round constants for the scalar key schedules (ARM has no key-assist instruction).
static const unsigned char kArmSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static const unsigned char kArmRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                           0x20, 0x40, 0x80, 0x1b, 0x36};

CHEATAH_TARGET("+crypto") inline void expand(const unsigned char key[16], uint8x16_t rk[11]) {
    unsigned char w[176];
    std::memcpy(w, key, 16);
    int r = 0;
    for (int i = 16; i < 176; i += 4) {
        unsigned char t[4] = {w[i - 4], w[i - 3], w[i - 2], w[i - 1]};
        if (i % 16 == 0) {
            const unsigned char a0 = t[0];
            t[0] = static_cast<unsigned char>(kArmSbox[t[1]] ^ kArmRcon[r++]);
            t[1] = kArmSbox[t[2]];
            t[2] = kArmSbox[t[3]];
            t[3] = kArmSbox[a0];
        }
        for (int j = 0; j < 4; ++j) w[i + j] = static_cast<unsigned char>(w[i - 16 + j] ^ t[j]);
    }
    for (int i = 0; i < 11; ++i) rk[i] = vld1q_u8(w + 16 * i);
}

// AES-256 key schedule (scalar, FIPS-197 §5.2) → 15 NEON round keys.
CHEATAH_TARGET("+crypto") inline void expand256(const unsigned char key[32], uint8x16_t rk[15]) {
    unsigned char w[240];
    std::memcpy(w, key, 32);
    int r = 0;
    for (int i = 32; i < 240; i += 4) {
        unsigned char t[4] = {w[i - 4], w[i - 3], w[i - 2], w[i - 1]};
        if (i % 32 == 0) {
            const unsigned char a0 = t[0];
            t[0] = static_cast<unsigned char>(kArmSbox[t[1]] ^ kArmRcon[r++]);
            t[1] = kArmSbox[t[2]];
            t[2] = kArmSbox[t[3]];
            t[3] = kArmSbox[a0];
        } else if (i % 32 == 16) {  // AES-256 extra SubWord
            for (int j = 0; j < 4; ++j) t[j] = kArmSbox[t[j]];
        }
        for (int j = 0; j < 4; ++j) w[i + j] = static_cast<unsigned char>(w[i - 32 + j] ^ t[j]);
    }
    for (int i = 0; i < 15; ++i) rk[i] = vld1q_u8(w + 16 * i);
}

// AES block encrypt with @p nr rounds (10 for AES-128, 14 for AES-256). ARM's AESE adds the round key
// at the START of the round.
CHEATAH_TARGET("+crypto") inline uint8x16_t enc_block(const uint8x16_t* rk, int nr, uint8x16_t s) {
    for (int i = 0; i < nr - 1; ++i) s = vaesmcq_u8(vaeseq_u8(s, rk[i]));
    return veorq_u8(vaeseq_u8(s, rk[nr - 1]), rk[nr]);
}

// Full 16-byte reversal (GCM is big-endian; mirrors the x86 bswap).
CHEATAH_TARGET("+crypto") inline uint8x16_t bswap(uint8x16_t x) {
    return vextq_u8(vrev64q_u8(x), vrev64q_u8(x), 8);
}

// Accumulate the carry-less product a·b into (lo, hi) + middle — PMULL is ARM's CLMUL.
CHEATAH_TARGET("+crypto") inline void clmul_acc(uint8x16_t a, uint8x16_t b, uint8x16_t& lo,
                                                uint8x16_t& hi, uint8x16_t& mid) {
    const poly64x2_t A = vreinterpretq_p64_u8(a), B = vreinterpretq_p64_u8(b);
    const poly64_t a0 = vgetq_lane_p64(A, 0), a1 = vgetq_lane_p64(A, 1);
    const poly64_t b0 = vgetq_lane_p64(B, 0), b1 = vgetq_lane_p64(B, 1);
    lo = veorq_u8(lo, vreinterpretq_u8_p128(vmull_p64(a0, b0)));
    hi = veorq_u8(hi, vreinterpretq_u8_p128(vmull_high_p64(A, B)));
    mid = veorq_u8(mid, vreinterpretq_u8_p128(vmull_p64(a1, b0)));
    mid = veorq_u8(mid, vreinterpretq_u8_p128(vmull_p64(a0, b1)));
}
// Fold the middle, reflect-shift (<<1), and reduce — a 1:1 NEON port of the x86 reduction.
CHEATAH_TARGET("+crypto") inline uint8x16_t reduce(uint8x16_t lo, uint8x16_t hi, uint8x16_t mid) {
    const uint8x16_t z = vdupq_n_u8(0);
    lo = veorq_u8(lo, vextq_u8(z, mid, 8));
    hi = veorq_u8(hi, vextq_u8(mid, z, 8));
    uint8x16_t t7 = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(lo), 31));
    uint8x16_t t8 = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(hi), 31));
    lo = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(lo), 1));
    hi = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(hi), 1));
    uint8x16_t t9 = vextq_u8(t7, z, 12);
    t8 = vextq_u8(z, t8, 12);
    t7 = vextq_u8(z, t7, 12);
    lo = vorrq_u8(lo, t7);
    hi = vorrq_u8(hi, t8);
    hi = vorrq_u8(hi, t9);
    t7 = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(lo), 31));
    t8 = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(lo), 30));
    t9 = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(lo), 25));
    t7 = veorq_u8(t7, t8);
    t7 = veorq_u8(t7, t9);
    t8 = vextq_u8(t7, z, 4);
    t7 = vextq_u8(z, t7, 4);
    lo = veorq_u8(lo, t7);
    uint8x16_t t2 = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(lo), 1));
    uint8x16_t t4 = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(lo), 2));
    uint8x16_t t5 = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(lo), 7));
    t2 = veorq_u8(t2, t4);
    t2 = veorq_u8(t2, t5);
    t2 = veorq_u8(t2, t8);
    lo = veorq_u8(lo, t2);
    return veorq_u8(hi, lo);
}
CHEATAH_TARGET("+crypto") inline uint8x16_t gfmul(uint8x16_t a, uint8x16_t b) {
    uint8x16_t lo = vdupq_n_u8(0), hi = vdupq_n_u8(0), mid = vdupq_n_u8(0);
    clmul_acc(a, b, lo, hi, mid);
    return reduce(lo, hi, mid);
}

// GHASH the buffer into Y, 8 blocks per reduction (Hp = {H..H^8}); single-block tail.
CHEATAH_TARGET("+crypto") inline uint8x16_t ghash_buf(uint8x16_t Y, const uint8x16_t Hp[8],
                                                      const unsigned char* p, std::size_t n) {
    std::size_t off = 0;
    for (; off + 128 <= n; off += 128) {
        uint8x16_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = bswap(vld1q_u8(p + off + 16 * i));
        uint8x16_t lo = vdupq_n_u8(0), hi = vdupq_n_u8(0), mid = vdupq_n_u8(0);
        clmul_acc(veorq_u8(Y, b[0]), Hp[7], lo, hi, mid);
        for (int i = 1; i < 8; ++i) clmul_acc(b[i], Hp[7 - i], lo, hi, mid);
        Y = reduce(lo, hi, mid);
    }
    for (; off < n; off += 16) {
        uint8x16_t blk;
        if (n - off >= 16) {
            blk = vld1q_u8(p + off);
        } else {
            unsigned char tmp[16] = {0};
            std::memcpy(tmp, p + off, n - off);
            blk = vld1q_u8(tmp);
        }
        Y = gfmul(veorq_u8(Y, bswap(blk)), Hp[0]);
    }
    return Y;
}

CHEATAH_TARGET("+crypto") inline uint8x16_t ctr_inc(uint8x16_t c) {
    alignas(16) unsigned char b[16];
    vst1q_u8(b, c);
    for (int j = 15; j >= 12; --j)
        if (++b[j] != 0) break;
    return vld1q_u8(b);
}

// Stitched CTR + GHASH, 8 blocks at a time (see the x86 path for the rationale).
CHEATAH_TARGET("+crypto") inline uint8x16_t ctr_ghash_stitch(const uint8x16_t* rk, int nr,
                                                             const uint8x16_t Hp[8], uint8x16_t ctr,
                                                             uint8x16_t Y, unsigned char* buf,
                                                             std::size_t len, bool encrypt) {
    std::size_t off = 0;
    for (; off + 128 <= len; off += 128) {
        uint8x16_t c[8];
        c[0] = ctr;
        for (int i = 1; i < 8; ++i) c[i] = ctr_inc(c[i - 1]);
        uint8x16_t g[8];
        for (int i = 0; i < 8; ++i) {
            const uint8x16_t in = vld1q_u8(buf + off + 16 * i);
            const uint8x16_t out = veorq_u8(in, enc_block(rk, nr, c[i]));
            vst1q_u8(buf + off + 16 * i, out);
            g[i] = bswap(encrypt ? out : in);
        }
        uint8x16_t lo = vdupq_n_u8(0), hi = vdupq_n_u8(0), mid = vdupq_n_u8(0);
        clmul_acc(veorq_u8(Y, g[0]), Hp[7], lo, hi, mid);
        for (int i = 1; i < 8; ++i) clmul_acc(g[i], Hp[7 - i], lo, hi, mid);
        Y = reduce(lo, hi, mid);
        ctr = ctr_inc(c[7]);
    }
    for (; off + 16 <= len; off += 16) {
        const uint8x16_t in = vld1q_u8(buf + off);
        const uint8x16_t out = veorq_u8(in, enc_block(rk, nr, ctr));
        vst1q_u8(buf + off, out);
        Y = gfmul(veorq_u8(Y, bswap(encrypt ? out : in)), Hp[0]);
        ctr = ctr_inc(ctr);
    }
    if (off < len) {
        const std::size_t m = len - off;
        alignas(16) unsigned char ksb[16];
        vst1q_u8(ksb, enc_block(rk, nr, ctr));
        alignas(16) unsigned char gb[16] = {0};
        for (std::size_t i = 0; i < m; ++i) {
            const unsigned char cin = buf[off + i];
            const unsigned char cout = static_cast<unsigned char>(cin ^ ksb[i]);
            gb[i] = encrypt ? cout : cin;
            buf[off + i] = cout;
        }
        Y = gfmul(veorq_u8(Y, bswap(vld1q_u8(gb))), Hp[0]);
    }
    return Y;
}

CHEATAH_TARGET("+crypto") inline void finalize_tag(const uint8x16_t* rk, int nr, const uint8x16_t Hp[8],
                                                   uint8x16_t J0, uint8x16_t Y, std::size_t aadlen,
                                                   std::size_t ctlen, unsigned char tag[16]) {
    alignas(16) unsigned char lb[16] = {0};
    const std::uint64_t abits = static_cast<std::uint64_t>(aadlen) * 8;
    const std::uint64_t cbits = static_cast<std::uint64_t>(ctlen) * 8;
    for (int i = 0; i < 8; ++i) {
        lb[7 - i] = static_cast<unsigned char>(abits >> (8 * i));
        lb[15 - i] = static_cast<unsigned char>(cbits >> (8 * i));
    }
    Y = veorq_u8(Y, bswap(vld1q_u8(lb)));
    Y = gfmul(Y, Hp[0]);
    vst1q_u8(tag, veorq_u8(bswap(Y), enc_block(rk, nr, J0)));
}

// Round keys (10 rounds for a 16-byte key, 14 for 32), Hp = {H … H^8}, and J0. Returns the round count.
CHEATAH_TARGET("+crypto") inline int setup(const unsigned char* key, std::size_t keylen,
                                           const unsigned char nonce[12], uint8x16_t* rk,
                                           uint8x16_t Hp[8], uint8x16_t& J0) {
    int nr;
    if (keylen == 32) { expand256(key, rk); nr = 14; }
    else { expand(key, rk); nr = 10; }
    const uint8x16_t H = bswap(enc_block(rk, nr, vdupq_n_u8(0)));
    Hp[0] = H;
    for (int i = 1; i < 8; ++i) Hp[i] = gfmul(Hp[i - 1], H);
    alignas(16) unsigned char j0[16] = {0};
    std::memcpy(j0, nonce, 12);
    j0[15] = 1;
    J0 = vld1q_u8(j0);
    return nr;
}

// AES-GCM encrypt for a 16- or 32-byte key (@p keylen); ciphertext with the 16-byte tag appended.
CHEATAH_TARGET("+crypto") inline std::string gcm_encrypt(const unsigned char* key, std::size_t keylen,
                                                         const unsigned char nonce[12],
                                                         std::string_view aad,
                                                         std::string_view plaintext) {
    uint8x16_t rk[15], Hp[8], J0;
    const int nr = setup(key, keylen, nonce, rk, Hp, J0);
    std::string ct(plaintext);
    uint8x16_t Y = ghash_buf(vdupq_n_u8(0), Hp,
                             reinterpret_cast<const unsigned char*>(aad.data()), aad.size());
    Y = ctr_ghash_stitch(rk, nr, Hp, ctr_inc(J0), Y, reinterpret_cast<unsigned char*>(&ct[0]),
                         ct.size(), /*encrypt=*/true);
    unsigned char tag[16];
    finalize_tag(rk, nr, Hp, J0, Y, aad.size(), ct.size(), tag);
    ct.append(reinterpret_cast<char*>(tag), 16);
    return ct;
}

// AES-GCM decrypt for a 16- or 32-byte key; constant-time tag check; "" on failure.
CHEATAH_TARGET("+crypto") inline std::string gcm_decrypt(const unsigned char* key, std::size_t keylen,
                                                         const unsigned char nonce[12],
                                                         std::string_view aad,
                                                         std::string_view ciphertext) {
    if (ciphertext.size() < 16) return "";
    const std::string_view ct = ciphertext.substr(0, ciphertext.size() - 16);
    const std::string_view want = ciphertext.substr(ciphertext.size() - 16);
    uint8x16_t rk[15], Hp[8], J0;
    const int nr = setup(key, keylen, nonce, rk, Hp, J0);
    std::string pt(ct);
    uint8x16_t Y = ghash_buf(vdupq_n_u8(0), Hp,
                             reinterpret_cast<const unsigned char*>(aad.data()), aad.size());
    Y = ctr_ghash_stitch(rk, nr, Hp, ctr_inc(J0), Y, reinterpret_cast<unsigned char*>(&pt[0]),
                         pt.size(), /*encrypt=*/false);
    unsigned char tag[16];
    finalize_tag(rk, nr, Hp, J0, Y, aad.size(), ct.size(), tag);
    unsigned char diff = 0;
    for (int i = 0; i < 16; ++i) diff |= tag[i] ^ static_cast<unsigned char>(want[i]);
    if (diff != 0) return "";
    return pt;
}

#ifndef CHEATAH_NO_CRYPTO_SELFTEST
CHEATAH_TARGET("+crypto") inline bool self_test() {
    static const unsigned char key[16] = {0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
                                          0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08};
    static const unsigned char nonce[12] = {0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
                                            0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88};
    static const unsigned char pt[60] = {
        0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26,
        0x9a, 0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda, 0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31,
        0x8a, 0x72, 0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49,
        0xa6, 0xb5, 0x25, 0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39};
    static const unsigned char aad[20] = {0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
                                          0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xab, 0xad, 0xda, 0xd2};
    static const unsigned char want[76] = {
        0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4,
        0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0, 0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac,
        0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac,
        0x84, 0xaa, 0x05, 0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91,
        0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb, 0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a,
        0x47};
    // AES-256 known-answer (McGrew GCM Test Case 16): gates the AES-256 hardware path too — a wrong
    // key schedule / 14-round enc on this CPU disables BOTH sizes and falls back to portable scalar.
    static const unsigned char key256[32] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08};
    static const unsigned char want256[76] = {
        0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07, 0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
        0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9, 0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
        0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d, 0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
        0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a, 0xbc, 0xc9, 0xf6, 0x62, 0x76, 0xfc, 0x6e, 0xce,
        0x0f, 0x4e, 0x17, 0x68, 0xcd, 0xdf, 0x88, 0x53, 0xbb, 0x2d, 0x55, 0x1b};
    const std::string_view aadv{reinterpret_cast<const char*>(aad), sizeof aad};
    const std::string_view ptv{reinterpret_cast<const char*>(pt), sizeof pt};
    const std::string ct = gcm_encrypt(key, 16, nonce, aadv, ptv);
    const std::string back = gcm_decrypt(key, 16, nonce, aadv, ct);
    const std::string ct256 = gcm_encrypt(key256, 32, nonce, aadv, ptv);
    const std::string back256 = gcm_decrypt(key256, 32, nonce, aadv, ct256);
    return ct.size() == sizeof want && std::memcmp(ct.data(), want, sizeof want) == 0 &&
           back.size() == sizeof pt && std::memcmp(back.data(), pt, sizeof pt) == 0 &&
           ct256.size() == sizeof want256 && std::memcmp(ct256.data(), want256, sizeof want256) == 0 &&
           back256.size() == sizeof pt && std::memcmp(back256.data(), pt, sizeof pt) == 0;
}
#endif  // CHEATAH_NO_CRYPTO_SELFTEST

inline bool available() {
#ifdef CHEATAH_NO_CRYPTO_SELFTEST
    static const bool ok = cpu_has_crypto();
#else
    static const bool ok = cpu_has_crypto() && self_test();
#endif
    return ok;
}

#else   // neither x86 nor ARMv8-crypto: no hardware path — the portable scalar reference runs.
/// Always false on this architecture: there is no AES hardware path, so callers fall back to the
/// portable scalar AES-128/256-GCM reference.
/// @return false (no hardware acceleration available here).
inline bool available() { return false; }
/// Unreachable stub so the accel namespace type-checks on non-accelerated targets; never called
/// because available() is false. Signature matches the accelerated gcm_encrypt.
/// @param key the AES key (ignored). @param keylen 16 or 32 (ignored). @param nonce the 12-byte IV
/// (ignored). @param aad additional authenticated data (ignored). @param plaintext the message (ignored).
/// @return the empty string (this path is never taken).
inline std::string gcm_encrypt(const unsigned char* key, std::size_t keylen,
                               const unsigned char nonce[12], std::string_view aad,
                               std::string_view plaintext) {
    (void)key; (void)keylen; (void)nonce; (void)aad; (void)plaintext;
    return {};
}
/// Unreachable stub so the accel namespace type-checks on non-accelerated targets; never called
/// because available() is false. Signature matches the accelerated gcm_decrypt.
/// @param key the AES key (ignored). @param keylen 16 or 32 (ignored). @param nonce the 12-byte IV
/// (ignored). @param aad additional authenticated data (ignored). @param ciphertext the ct+tag (ignored).
/// @return the empty string (this path is never taken).
inline std::string gcm_decrypt(const unsigned char* key, std::size_t keylen,
                               const unsigned char nonce[12], std::string_view aad,
                               std::string_view ciphertext) {
    (void)key; (void)keylen; (void)nonce; (void)aad; (void)ciphertext;
    return {};
}
#endif  // arch dispatch

}  // namespace cheatah::aead::accel
