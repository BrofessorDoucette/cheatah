// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::json::detail — the low-level JSON scanners, header-only so BOTH the dynamic DOM
// parser (json.cpp) and the static struct reader (read.hpp) share exactly one implementation. These
// operate on a Cursor and never allocate except where noted (decode_escapes / the caller's string).
// SIMD whitespace/string scanning comes from simd.hpp.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "cursor.hpp"
#include "simd.hpp"

namespace cheatah::parsers::json::detail {

// Advance the cursor past JSON whitespace (SIMD-accelerated; see simd.hpp).
// @complexity O(whitespace run)  @alloc none  @test Json.ParseObject
inline void skip_ws(Cursor& c) noexcept {
    c.it = simd::skip_whitespace(c.it, c.end);
}

// Consume the exact literal `lit` (e.g. "true") if present, else leave the cursor put.
// @complexity O(|lit|)  @alloc none  @test Json.ParseObject
inline bool match(Cursor& c, std::string_view lit) noexcept {
    if (static_cast<std::size_t>(c.end - c.it) < lit.size()) {
        return false;
    }
    if (std::string_view(c.it, lit.size()) != lit) {
        return false;
    }
    c.it += lit.size();
    return true;
}

// ---- string scanning + escape decoding --------------------------------------

// Append the UTF-8 encoding of code point `cp` (1..4 bytes) to `out`.
// @complexity O(1)  @alloc amortized growth of `out`  @test JsonRead.Strings
inline void append_utf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Read 4 hex digits at raw[i..i+4) into cp.
// @complexity O(1)  @alloc none  @test JsonRead.Strings
inline bool hex4(std::string_view raw, std::size_t i, std::uint32_t& cp) {
    if (i + 4 > raw.size()) {
        return false;
    }
    std::uint32_t v = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const char ch = raw[i + k];
        v <<= 4;
        if (ch >= '0' && ch <= '9') {
            v |= static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            v |= static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            v |= static_cast<std::uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    cp = v;
    return true;
}

// Decode the raw (escaped) inner bytes of a JSON string into `out`. Ordinary bytes between escapes
// are copied in BULK (find the next backslash, append the whole run) rather than one at a time —
// most of a string is non-escape, so this is a few memcpy-sized appends instead of N push_backs.
// @complexity O(|raw|)  @alloc `out` growth (reserved once up front)  @test JsonRead.Strings
inline bool decode_escapes(std::string_view raw, std::string& out) {
    out.clear();
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        const std::size_t bs = raw.find('\\', i);  // next escape, or npos
        const std::size_t run_end = (bs == std::string_view::npos) ? raw.size() : bs;
        out.append(raw.data() + i, run_end - i);  // bulk-copy the ordinary run
        if (bs == std::string_view::npos) {
            return true;
        }
        i = bs + 1;  // step onto the escape selector
        if (i >= raw.size()) {
            return false;
        }
        switch (raw[i++]) {  // consume the selector; i now points past it
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!hex4(raw, i, cp)) {  // the 4 hex digits at [i, i+4)
                    return false;
                }
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate; expect a low surrogate \uXXXX
                    std::uint32_t lo = 0;
                    if (i + 1 >= raw.size() || raw[i] != '\\' || raw[i + 1] != 'u' ||
                        !hex4(raw, i + 2, lo) || lo < 0xDC00 || lo > 0xDFFF) {
                        return false;
                    }
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                append_utf8(cp, out);
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

// Scan a "..." string: set `raw` to its inner bytes and `esc` to whether it had escapes;
// on entry c.it is at the opening quote, on success c.it is past the closing quote.
// @complexity O(|string|) (SIMD 32 bytes/step)  @alloc none (raw is a view)  @test Json.Strings
inline bool scan_string(Cursor& c, std::string_view& raw, bool& esc) {
    ++c.it;  // skip opening quote
    const char* const start = c.it;
    esc = false;
    while (c.it < c.end) {
        // SIMD-jump over ordinary content straight to the next quote or backslash (see simd.hpp).
        c.it = simd::find_quote_or_backslash(c.it, c.end);
        if (c.it == c.end) {
            break;  // unterminated
        }
        if (*c.it == '"') {
            raw = std::string_view(start, static_cast<std::size_t>(c.it - start));
            ++c.it;  // skip closing quote
            return true;
        }
        esc = true;  // a backslash: this string has escapes (decoded later)
        if (c.end - c.it < 2) {
            return false;  // dangling escape at end of input (checked BEFORE advancing — a pointer
                           // more than one-past-the-end is undefined behavior, even unused)
        }
        c.it += 2;  // skip escape + escaped char (so \" does not end the string)
    }
    return false;  // unterminated
}

// ---- scalars ----------------------------------------------------------------

// Powers of ten 10^0 .. 10^22 — every one is EXACTLY representable as a double (5^22 < 2^53), which
// is what makes the float fast path below correctly rounded.
inline constexpr double kPow10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                      1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                      1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// Parse a JSON double the fast way (Clinger): accumulate the digits into an integer mantissa, then
// scale by one power of ten. When the mantissa fits in 53 bits AND the scale is within ±22, both
// are exact doubles, so the single multiply/divide rounds once — bit-identical to std::from_chars
// but without its general-format machinery. Anything outside that window (20+ digits, huge
// exponents) falls back to std::from_chars for full correctness. Typical JSON numbers — prices,
// quantities, timestamps — live entirely in the fast window.
// @complexity O(digits)  @alloc none  @test JsonRead.NumbersEdge
inline bool parse_double_fast(Cursor& c, double& out) {
    const char* p = c.it;
    const char* const end = c.end;
    const bool negative = (p != end && *p == '-');
    if (negative) {
        ++p;
    }

    // Integer digits, then optional ".fraction" — both accumulate into ONE integer mantissa;
    // each fraction digit just shifts the decimal exponent down by one.
    const char* const int_start = p;
    std::uint64_t mantissa = 0;
    while (p != end && static_cast<unsigned>(*p - '0') <= 9u) {
        mantissa = mantissa * 10u + static_cast<unsigned>(*p - '0');
        ++p;
    }
    if (p == int_start) {
        return false;  // JSON requires at least one integer digit
    }
    std::int64_t digit_count = p - int_start;
    int exp10 = 0;
    if (p != end && *p == '.') {
        ++p;
        const char* const frac_start = p;
        while (p != end && static_cast<unsigned>(*p - '0') <= 9u) {
            mantissa = mantissa * 10u + static_cast<unsigned>(*p - '0');
            ++p;
        }
        if (p == frac_start) {
            return false;  // JSON requires a digit after the decimal point
        }
        exp10 -= static_cast<int>(p - frac_start);
        digit_count += p - frac_start;
    }

    // Optional exponent ("e"/"E", optional sign, digits).
    if (p != end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool exp_negative = false;
        if (p != end && (*p == '+' || *p == '-')) {
            exp_negative = (*p == '-');
            ++p;
        }
        const char* const exp_start = p;
        int e = 0;
        while (p != end && static_cast<unsigned>(*p - '0') <= 9u) {
            if (e < 10000) {  // clamp; anything this large leaves the fast window anyway
                e = e * 10 + (*p - '0');
            }
            ++p;
        }
        if (p == exp_start) {
            return false;  // 'e' with no digits
        }
        exp10 += exp_negative ? -e : e;
    }

    // The exactness window: <=19 digits means the mantissa accumulated without u64 overflow, the
    // 2^53 test means it is an exact double, and |exp10| <= 22 means the scale is an exact double.
    if (digit_count <= 19 && mantissa < (1ull << 53) && -22 <= exp10 && exp10 <= 22) {
        double value = static_cast<double>(mantissa);
        value = (exp10 >= 0) ? value * kPow10[exp10] : value / kPow10[-exp10];
        out = negative ? -value : value;
        c.it = p;
        return true;
    }

    // Outside the window: std::from_chars handles arbitrary precision/exponents correctly.
    const std::from_chars_result r = std::from_chars(c.it, end, out);
    if (r.ec != std::errc()) {
        return false;
    }
    c.it = r.ptr;
    return true;
}

// Parse a JSON number into an arithmetic `out`. INTEGRAL types use a tight base-10 loop; double
// uses the Clinger fast path above; other floating types (float, long double) defer to
// std::from_chars. The integral loop is overflow-safe: a literal with more digits than the type can
// ever hold exactly is re-parsed by std::from_chars, which detects out-of-range exactly; a negative
// literal is rejected for unsigned fields rather than wrapped.
// @complexity O(digits)  @alloc none  @test JsonRead.Scalars
template <class T>
inline bool parse_arithmetic(Cursor& c, T& out) {
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        const char* p = c.it;
        const bool negative = (p != c.end && *p == '-');
        if (negative) {
            if constexpr (std::is_unsigned_v<T>) {
                return false;  // a negative literal cannot fit an unsigned field
            }
            ++p;
        }
        if (p == c.end || *p < '0' || *p > '9') {
            return false;  // a number must have at least one digit
        }
        const char* const digit_start = p;
        std::make_unsigned_t<T> magnitude = 0;
        do {
            magnitude = magnitude * 10 + static_cast<unsigned>(*p - '0');
            ++p;
        } while (p != c.end && *p >= '0' && *p <= '9');
        if (p - digit_start > std::numeric_limits<T>::digits10) {
            // More digits than T holds exactly: the accumulation above may have wrapped. Rare —
            // re-parse with std::from_chars, which detects overflow exactly (and rejects it).
            const std::from_chars_result r = std::from_chars(c.it, c.end, out);
            if (r.ec != std::errc()) {
                return false;
            }
            c.it = r.ptr;
            return true;
        }
        c.it = p;
        out = negative ? static_cast<T>(0) - static_cast<T>(magnitude) : static_cast<T>(magnitude);
        return true;
    } else if constexpr (std::is_same_v<T, double>) {
        return parse_double_fast(c, out);
    } else {
        const std::from_chars_result r = std::from_chars(c.it, c.end, out);
        if (r.ec != std::errc()) {
            return false;
        }
        c.it = r.ptr;
        return true;
    }
}

// ---- skip an unknown value (iterative; depth-counted, so it cannot be stack-overflowed) ---------
//
// Consume exactly one complete JSON value (any shape) without storing it — used by the struct
// reader to discard keys not present in the schema. Strings are skipped whole (so braces inside
// them never miscount); containers are balanced with a depth counter rather than recursion, so an
// adversarially deep unknown value costs O(depth) iterations and O(1) stack.
// @complexity O(skipped bytes)  @alloc none  @test JsonRead.UnknownKeys
inline bool skip_value(Cursor& c) {
    std::size_t depth = 0;
    do {
        skip_ws(c);
        if (c.it == c.end) {
            return false;
        }
        switch (*c.it) {
            case '{':
            case '[':
                ++c.it;
                ++depth;
                break;
            case '}':
            case ']':
                if (depth == 0) {
                    return false;
                }
                ++c.it;
                --depth;
                break;
            case '"': {
                std::string_view raw;
                bool esc = false;
                if (!scan_string(c, raw, esc)) {
                    return false;
                }
                break;
            }
            case ',':
            case ':':
                if (depth == 0) {
                    return false;  // stray punctuation is not a value
                }
                ++c.it;  // structural punctuation inside a container we're skipping
                break;
            case 't':
                if (!match(c, "true")) return false;
                break;
            case 'f':
                if (!match(c, "false")) return false;
                break;
            case 'n':
                if (!match(c, "null")) return false;
                break;
            default: {
                double scratch = 0.0;  // value discarded; we only need to advance past the number
                if (!parse_arithmetic(c, scratch)) {
                    return false;
                }
                break;
            }
        }
    } while (depth > 0);
    return true;
}

}  // namespace cheatah::parsers::json::detail
