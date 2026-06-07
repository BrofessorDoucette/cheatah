#pragma once

/**
 * @file string.hpp
 * @brief cheatah `string` — text operations + Python's `string` constants,
 *        surfaced as free functions (a .purr program writes `string.upper("x")`).
 *
 * `import string` includes this header and links `libcheatah_string`. Unit tests:
 * `stdlib/tests/string_test.cpp`; the suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Functions returning `std::string` / `std::vector<std::string>` allocate
 *       their result on the heap; the predicate/index functions (returning `bool`/
 *       `long`) do not allocate. `n` below is the input length.
 */
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::string {

// ---- constants (Python `string` module) ----
inline constexpr std::string_view ascii_lowercase = "abcdefghijklmnopqrstuvwxyz";  ///< `a–z`.
inline constexpr std::string_view ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";  ///< `A–Z`.
inline constexpr std::string_view ascii_letters =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";   ///< `a–zA–Z`.
inline constexpr std::string_view digits = "0123456789";                  ///< `0–9`.
inline constexpr std::string_view hexdigits = "0123456789abcdefABCDEF";   ///< hex digits.
inline constexpr std::string_view octdigits = "01234567";                 ///< octal digits.
inline constexpr std::string_view punctuation = R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";  ///< ASCII punctuation.
inline constexpr std::string_view whitespace = " \t\n\r\f\v";             ///< ASCII whitespace.

// ---- case ----
/** Uppercase. @param s input. @return @p s uppercased. @note O(n); allocates the result. @test CheatahString.Case */
std::string upper(std::string_view s);
/** Lowercase. @param s input. @return @p s lowercased. @note O(n); allocates the result. @test CheatahString.Case */
std::string lower(std::string_view s);
/** Capitalize: first char upper, rest lower. @param s input. @return capitalized @p s. @note O(n); allocates. @test CheatahString.Case */
std::string capitalize(std::string_view s);
/** Title-case each word. @param s input. @return title-cased @p s. @note O(n); allocates. @test CheatahString.Case */
std::string title(std::string_view s);
/** Swap the case of each letter. @param s input. @return case-swapped @p s. @note O(n); allocates. @test CheatahString.Case */
std::string swapcase(std::string_view s);

// ---- trimming (default: ASCII whitespace) ----
/** Strip leading+trailing @p chars. @param s input. @param chars cut set. @return trimmed @p s. @note O(n); allocates. @test CheatahString.Trimming */
std::string strip(std::string_view s, std::string_view chars = whitespace);
/** Strip leading @p chars. @param s input. @param chars cut set. @return left-trimmed @p s. @note O(n); allocates. @test CheatahString.Trimming */
std::string lstrip(std::string_view s, std::string_view chars = whitespace);
/** Strip trailing @p chars. @param s input. @param chars cut set. @return right-trimmed @p s. @note O(n); allocates. @test CheatahString.Trimming */
std::string rstrip(std::string_view s, std::string_view chars = whitespace);

// ---- search / test ----
/** Prefix test. @param s input. @param prefix sought prefix. @return true iff @p s starts with @p prefix. @note O(n); no heap. @test CheatahString.SearchAndTest */
bool startswith(std::string_view s, std::string_view prefix);
/** Suffix test. @param s input. @param suffix sought suffix. @return true iff @p s ends with @p suffix. @note O(n); no heap. @test CheatahString.SearchAndTest */
bool endswith(std::string_view s, std::string_view suffix);
/** Substring test. @param s input. @param sub needle. @return true iff @p sub occurs in @p s. @note O(n·m); no heap. @test CheatahString.SearchAndTest */
bool contains(std::string_view s, std::string_view sub);
/** First index of @p sub. @param s input. @param sub needle. @return index, or -1. @note O(n·m); no heap. @test CheatahString.SearchAndTest */
long find(std::string_view s, std::string_view sub);
/** Last index of @p sub. @param s input. @param sub needle. @return index, or -1. @note O(n·m); no heap. @test CheatahString.SearchAndTest */
long rfind(std::string_view s, std::string_view sub);
/** Count non-overlapping @p sub. @param s input. @param sub needle. @return occurrence count. @note O(n·m); no heap. @test CheatahString.SearchAndTest */
long count(std::string_view s, std::string_view sub);

// ---- transform ----
/** Replace all @p from with @p to. @param s input. @param from,to needle/replacement. @return new string. @note O(n·m); allocates. @test CheatahString.Transform */
std::string replace(std::string_view s, std::string_view from, std::string_view to);
/** Split on @p sep. @param s input. @param sep separator (empty → the whole string as one part). @return the parts. @note O(n); allocates a vector of strings. @test CheatahString.Transform, CheatahString.SplitEmptySeparator */
std::vector<std::string> split(std::string_view s, std::string_view sep);
/** Split on runs of whitespace. @param s input. @return the non-empty parts. @note O(n); allocates a vector of strings. @test CheatahString.Transform */
std::vector<std::string> split(std::string_view s);
/** Split into lines. @param s input. @return the lines (newlines removed). @note O(n); allocates a vector of strings. @test CheatahString.Transform */
std::vector<std::string> splitlines(std::string_view s);
/** Python `string.capwords`: split on whitespace, capitalize, re-join with spaces. @param s input. @return the result. @note O(n); allocates. @test CheatahString.Transform */
std::string capwords(std::string_view s);

/// StringViewable<T>: a `std::string_view` can be built from T — what join() needs.
template <typename T>
concept StringViewable = requires(const T& v) { std::string_view(v); };

/** Join @p parts with @p sep. @param sep separator. @param parts any range of string-like values. @return the joined string. @note O(total length); allocates the result. @test CheatahString.Transform */
template <std::ranges::input_range Range>
    requires StringViewable<std::ranges::range_value_t<Range>>
std::string join(std::string_view sep, const Range& parts) {
    std::string out;
    bool first = true;
    for (const auto& part : parts) {
        if (!first) {
            out += sep;
        }
        out += std::string_view(part);
        first = false;
    }
    return out;
}

// ---- padding (fill defaults to a space; first character of `fill` is used) ----
/** Left-justify to @p width. @param s input. @param width target. @param fill pad char. @return padded @p s (or @p s if already ≥ width). @note O(width); allocates. @test CheatahString.Padding */
std::string ljust(std::string_view s, std::size_t width, std::string_view fill = " ");
/** Right-justify to @p width. @param s input. @param width target. @param fill pad char. @return padded @p s. @note O(width); allocates. @test CheatahString.Padding */
std::string rjust(std::string_view s, std::size_t width, std::string_view fill = " ");
/** Center within @p width. @param s input. @param width target. @param fill pad char. @return padded @p s. @note O(width); allocates. @test CheatahString.Padding */
std::string center(std::string_view s, std::size_t width, std::string_view fill = " ");
/** Zero-fill on the left to @p width. @param s input. @param width target. @return `'0'`-padded @p s. @note O(width); allocates. @test CheatahString.Padding */
std::string zfill(std::string_view s, std::size_t width);

// ---- whole-string classification (False for the empty string, like Python) ----
/** All digits? @param s input. @return true iff non-empty and all `0–9`. @note O(n); no heap. @test CheatahString.Classification */
bool isdigit(std::string_view s);
/** All letters? @param s input. @return true iff non-empty and all alphabetic. @note O(n); no heap. @test CheatahString.Classification */
bool isalpha(std::string_view s);
/** All alphanumeric? @param s input. @return true iff non-empty and all letters/digits. @note O(n); no heap. @test CheatahString.ClassificationAlnumAndSpace */
bool isalnum(std::string_view s);
/** All whitespace? @param s input. @return true iff non-empty and all whitespace. @note O(n); no heap. @test CheatahString.ClassificationAlnumAndSpace */
bool isspace(std::string_view s);
/** All uppercase? @param s input. @return true iff non-empty and has no lowercase. @note O(n); no heap. @test CheatahString.Classification */
bool isupper(std::string_view s);
/** All lowercase? @param s input. @return true iff non-empty and has no uppercase. @note O(n); no heap. @test CheatahString.Classification */
bool islower(std::string_view s);

} // namespace cheatah::string
