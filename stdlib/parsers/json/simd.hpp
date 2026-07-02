#pragma once

// cheatah::parsers::json::simd — SIMD primitives for the parser, isolated here so json.cpp stays
// free of intrinsics. Every primitive has a portable SCALAR fallback, so this header compiles on
// ANY target; on x86 with AVX2 it uses 256-bit byte operations. The functions are small and
// `inline`, so the compiler folds them into their call sites (verified: they inline into skip_ws,
// which itself inlines into the parser). No runtime polymorphism.

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace cheatah::parsers::json::simd {

// Is `ch` one of the four JSON whitespace bytes (space, tab, newline, carriage return)?
// @complexity O(1)  @alloc none  @test Json.ParseObject
[[nodiscard]] constexpr bool is_whitespace(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

// Return the first non-whitespace byte at or after `it`, or `end` if the rest is all whitespace.
// AVX2 scans 32 bytes per step — mark the whitespace lanes, then jump to the first non-whitespace;
// the scalar loop handles the < 32-byte tail (and the whole scan without AVX2). Identical result.
// @complexity O(run length) (32 bytes/step under AVX2)  @alloc none  @test Json.ParseObject
[[nodiscard]] inline const char* skip_whitespace(const char* it, const char* end) noexcept {
    // Fast path: compact JSON (the common case) has no whitespace between tokens, so the first byte
    // is already non-whitespace — one test and return, with no SIMD setup. Only a genuine run of
    // whitespace (pretty-printed input) falls through to the 32-byte vector scan below.
    if (it == end || !is_whitespace(*it)) {
        return it;
    }
#if defined(__AVX2__)
    const __m256i kSpace = _mm256_set1_epi8(' ');
    const __m256i kTab = _mm256_set1_epi8('\t');
    const __m256i kNewline = _mm256_set1_epi8('\n');
    const __m256i kReturn = _mm256_set1_epi8('\r');
    while (end - it >= 32) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(it));
        const __m256i ws = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(v, kSpace), _mm256_cmpeq_epi8(v, kTab)),
            _mm256_or_si256(_mm256_cmpeq_epi8(v, kNewline), _mm256_cmpeq_epi8(v, kReturn)));
        // movemask: bit i = 1 where byte i is whitespace; invert to find the first NON-whitespace.
        const unsigned non_ws = ~static_cast<unsigned>(_mm256_movemask_epi8(ws));
        if (non_ws != 0) {
            return it + __builtin_ctz(non_ws);  // first non-whitespace lane in this chunk
        }
        it += 32;  // the whole 32-byte chunk was whitespace
    }
#endif
    while (it < end && is_whitespace(*it)) {
        ++it;
    }
    return it;
}

// Return the first byte at or after `it` that is a double-quote or a backslash — the only two bytes
// that matter while scanning a JSON string body (a closing quote ends it; a backslash starts an
// escape) — or `end` if neither occurs. AVX2 tests 32 bytes per step; the scalar loop handles the
// < 32-byte tail (and the whole scan without AVX2). The caller decides which byte it found.
// @complexity O(distance to hit) (32 bytes/step under AVX2)  @alloc none  @test Json.Strings
[[nodiscard]] inline const char* find_quote_or_backslash(const char* it, const char* end) noexcept {
#if defined(__AVX2__)
    const __m256i kQuote = _mm256_set1_epi8('"');
    const __m256i kBackslash = _mm256_set1_epi8('\\');
    while (end - it >= 32) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(it));
        const __m256i hit =
            _mm256_or_si256(_mm256_cmpeq_epi8(v, kQuote), _mm256_cmpeq_epi8(v, kBackslash));
        // movemask: bit i = 1 where byte i is a quote or backslash; ctz finds the first one.
        const unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(hit));
        if (mask != 0) {
            return it + __builtin_ctz(mask);  // first quote/backslash lane in this chunk
        }
        it += 32;  // the whole 32-byte chunk was ordinary string content
    }
#endif
    while (it < end && *it != '"' && *it != '\\') {
        ++it;
    }
    return it;
}

}  // namespace cheatah::parsers::json::simd
