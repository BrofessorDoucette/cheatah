// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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
/// ASCII punctuation.
inline constexpr std::string_view punctuation = R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";
inline constexpr std::string_view whitespace = " \t\n\r\f\v";             ///< ASCII whitespace.

// ---- case ----
/**
 * Uppercase.
 *
 * Returns a new string with every ASCII lowercase letter mapped to uppercase
 * via `std::toupper`; non-letters and bytes ≥ 0x80 are copied unchanged (ASCII-only).
 * @param s input.
 * @return @p s uppercased.
 * @complexity O(n).
 * @alloc allocates the result.
 * @test CheatahString.Case
 * @crtest StringCompileRun.Upper
 * @systest StdlibE2E.String
 */
std::string upper(std::string_view s);
/**
 * Lowercase.
 *
 * Returns a new string with every ASCII uppercase letter mapped to lowercase
 * via `std::tolower`; non-letters and bytes ≥ 0x80 are copied unchanged (ASCII-only).
 * @param s input.
 * @return @p s lowercased.
 * @complexity O(n).
 * @alloc allocates the result.
 * @test CheatahString.Case
 * @crtest StringCompileRun.Lower
 * @systest StdlibE2E.String
 */
std::string lower(std::string_view s);
/**
 * Capitalize: first char upper, rest lower.
 *
 * Uppercases the first character and lowercases all remaining characters (ASCII-only);
 * an empty input is returned unchanged.
 * @param s input.
 * @return capitalized @p s.
 * @complexity O(n).
 * @alloc allocates.
 * @test CheatahString.Case
 * @crtest StringCompileRun.Capitalize
 * @systest StdlibE2E.String
 */
std::string capitalize(std::string_view s);
/**
 * Title-case each word.
 *
 * Uppercases the first letter of every run of letters and lowercases the rest;
 * any non-letter (digits, punctuation, whitespace) acts as a word boundary (ASCII-only).
 * @param s input.
 * @return title-cased @p s.
 * @complexity O(n).
 * @alloc allocates.
 * @test CheatahString.Case
 * @crtest StringCompileRun.Title
 * @systest StdlibE2E.String
 */
std::string title(std::string_view s);
/**
 * Swap the case of each letter.
 *
 * Returns a new string with each ASCII letter's case inverted; non-letters are
 * left unchanged (ASCII-only).
 * @param s input.
 * @return case-swapped @p s.
 * @complexity O(n).
 * @alloc allocates.
 * @test CheatahString.Case
 * @crtest StringCompileRun.Swapcase
 * @systest StdlibE2E.String
 */
std::string swapcase(std::string_view s);

// ---- trimming (default: ASCII whitespace) ----
/**
 * Strip leading+trailing @p chars.
 *
 * Removes characters from both ends as long as each is present in the @p chars
 * set (the set is a bag of characters, not a substring); defaults to ASCII
 * whitespace. An empty @p chars set strips nothing.
 * @param s input.
 * @param chars cut set.
 * @return trimmed @p s.
 * @complexity O(n·m) (m = size of the @p chars set; a constant for the default).
 * @alloc allocates the result plus lstrip's intermediate string.
 * @test CheatahString.Trimming
 * @crtest StringCompileRun.Strip
 * @systest StdlibE2E.String
 */
std::string strip(std::string_view s, std::string_view chars = whitespace);
/**
 * Strip leading @p chars.
 *
 * Removes characters from the front only, as long as each is in the @p chars set
 * (a bag of characters, not a substring); defaults to ASCII whitespace.
 * @param s input.
 * @param chars cut set.
 * @return left-trimmed @p s.
 * @complexity O(n·m) (m = size of the @p chars set; a constant for the default).
 * @alloc allocates.
 * @test CheatahString.Trimming
 * @crtest StringCompileRun.Lstrip
 * @systest StdlibE2E.String
 */
std::string lstrip(std::string_view s, std::string_view chars = whitespace);
/**
 * Strip trailing @p chars.
 *
 * Removes characters from the end only, as long as each is in the @p chars set
 * (a bag of characters, not a substring); defaults to ASCII whitespace.
 * @param s input.
 * @param chars cut set.
 * @return right-trimmed @p s.
 * @complexity O(n·m) (m = size of the @p chars set; a constant for the default).
 * @alloc allocates.
 * @test CheatahString.Trimming
 * @crtest StringCompileRun.Rstrip
 * @systest StdlibE2E.String
 */
std::string rstrip(std::string_view s, std::string_view chars = whitespace);

// ---- search / test ----
/**
 * Prefix test.
 *
 * Case-sensitive, byte-exact comparison; an empty @p prefix always matches.
 * @param s input.
 * @param prefix sought prefix.
 * @return true iff @p s starts with @p prefix.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Startswith
 * @systest StdlibE2E.String
 */
bool startswith(std::string_view s, std::string_view prefix);
/**
 * Suffix test.
 *
 * Case-sensitive, byte-exact comparison; an empty @p suffix always matches.
 * @param s input.
 * @param suffix sought suffix.
 * @return true iff @p s ends with @p suffix.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Endswith
 * @systest StdlibE2E.String
 */
bool endswith(std::string_view s, std::string_view suffix);
/**
 * Substring test.
 *
 * Case-sensitive search for @p sub anywhere in @p s; an empty @p sub is always
 * considered present.
 * @param s input.
 * @param sub needle.
 * @return true iff @p sub occurs in @p s.
 * @complexity O(n·m).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Contains
 * @systest StdlibE2E.String
 */
bool contains(std::string_view s, std::string_view sub);

/**
 * contains() with a single-char needle — what iterating a string yields (`for ch in s`).
 * @param s input. @param c the character. @return true when present.
 * @complexity O(n). @alloc none.
 * @test CheatahString.ContainsChar
 * @crtest StringCompileRun.Contains
 * @systest StdlibE2E.String
 */
inline bool contains(std::string_view s, char c) { return s.find(c) != std::string_view::npos; }
/**
 * First index of @p sub.
 *
 * Returns the 0-based byte index of the first (leftmost) case-sensitive match,
 * or -1 if not found; an empty @p sub returns 0.
 * @param s input.
 * @param sub needle.
 * @return index, or -1.
 * @complexity O(n·m).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Find
 * @systest StdlibE2E.String
 */
long find(std::string_view s, std::string_view sub);
/**
 * First index of @p sub at or after @p start.
 *
 * Like @ref find but begins the search at byte offset @p start (matching Python's
 * `str.find(sub, start)`): a negative @p start is treated as 0, and a @p start past the
 * end returns -1. Lets a caller scan a large buffer for successive matches WITHOUT slicing
 * the tail each step — turning an otherwise O(n²) repeated-search loop into O(n).
 * @param s input.
 * @param sub needle.
 * @param start byte offset to begin searching from.
 * @return index (absolute, into @p s), or -1.
 * @complexity O(n·m).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Find
 * @systest StdlibE2E.String
 */
long find(std::string_view s, std::string_view sub, long start);
/**
 * Last index of @p sub.
 *
 * Returns the 0-based byte index of the last (rightmost) case-sensitive match,
 * or -1 if not found; an empty @p sub returns the length of @p s.
 * @param s input.
 * @param sub needle.
 * @return index, or -1.
 * @complexity O(n·m).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Rfind
 * @systest StdlibE2E.String
 */
long rfind(std::string_view s, std::string_view sub);
/**
 * Count non-overlapping @p sub.
 *
 * Counts left-to-right, non-overlapping case-sensitive matches; matching Python,
 * an empty @p sub returns `len(s) + 1`.
 * @param s input.
 * @param sub needle.
 * @return occurrence count.
 * @complexity O(n·m).
 * @alloc none.
 * @test CheatahString.SearchAndTest
 * @crtest StringCompileRun.Count
 * @systest StdlibE2E.String
 */
long count(std::string_view s, std::string_view sub);

// ---- transform ----
/**
 * Replace all @p from with @p to.
 *
 * Replaces every non-overlapping, case-sensitive occurrence of @p from with @p to;
 * an empty @p from leaves @p s unchanged (unlike Python).
 * @param s input.
 * @param from,to needle/replacement.
 * @return new string.
 * @complexity O(n·m + result length).
 * @alloc allocates.
 * @test CheatahString.Transform
 * @crtest StringCompileRun.Replace
 * @systest StdlibE2E.String
 */
std::string replace(std::string_view s, std::string_view from, std::string_view to);
/**
 * Split on @p sep.
 *
 * Splits at each non-overlapping occurrence of @p sep, keeping empty fields
 * (e.g. "a,,b" yields three parts, leading/trailing separators yield empty
 * strings); the result always has at least one element.
 * @param s input.
 * @param sep separator (empty → the whole string as one part).
 * @return the parts.
 * @complexity O(n·m).
 * @alloc allocates a vector of strings.
 * @test CheatahString.Transform, CheatahString.SplitEmptySeparator
 * @crtest StringCompileRun.Split
 * @systest StdlibE2E.String
 */
std::vector<std::string> split(std::string_view s, std::string_view sep);
/**
 * Split on runs of whitespace.
 *
 * Splits on maximal runs of ASCII whitespace and discards empty fields, so leading,
 * trailing, and repeated whitespace produce no empty parts; a blank/empty input
 * yields an empty vector.
 * @param s input.
 * @return the non-empty parts.
 * @complexity O(n).
 * @alloc allocates a vector of strings.
 * @test CheatahString.Transform
 * @crtest StringCompileRun.SplitWhitespace
 * @systest StdlibE2E.String
 */
std::vector<std::string> split(std::string_view s);
/**
 * Split into lines.
 *
 * Breaks on `\n`, `\r`, and `\r\n` (treated as a single break) with the line
 * terminators removed; a trailing newline does not produce a final empty line,
 * and an empty input yields an empty vector.
 * @param s input.
 * @return the lines (newlines removed).
 * @complexity O(n).
 * @alloc allocates a vector of strings.
 * @test CheatahString.Transform
 * @crtest StringCompileRun.Splitlines
 * @systest StdlibE2E.String
 */
std::vector<std::string> splitlines(std::string_view s);
/**
 * Python `string.capwords`: split on whitespace, capitalize, re-join with spaces.
 *
 * Capitalizes each whitespace-delimited word (first letter upper, rest lower) and
 * re-joins with single spaces, so all runs of original whitespace collapse and
 * leading/trailing whitespace is dropped.
 * @param s input.
 * @return the result.
 * @complexity O(n).
 * @alloc allocates a vector of words plus the result.
 * @test CheatahString.Transform
 * @crtest StringCompileRun.Capwords
 * @systest StdlibE2E.String
 */
std::string capwords(std::string_view s);

/// StringViewable<T>: a `std::string_view` can be built from T — what join() needs.
template <typename T>
concept StringViewable = requires(const T& v) { std::string_view(v); };

/**
 * Join @p parts with @p sep.
 *
 * Concatenates each element of @p parts with @p sep inserted only between elements
 * (no leading or trailing separator); an empty range yields an empty string.
 * @param sep separator.
 * @param parts any range of string-like values.
 * @return the joined string.
 * @complexity O(total length).
 * @alloc allocates the result.
 * @test CheatahString.Transform
 * @crtest StringCompileRun.Join
 * @systest StdlibE2E.String
 */
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
/**
 * Left-justify to @p width.
 *
 * Pads @p s on the right with the fill character up to @p width; if @p s is already
 * at least @p width long it is returned unchanged. Only the first character of
 * @p fill is used (an empty @p fill defaults to a space).
 * @param s input.
 * @param width target.
 * @param fill pad char.
 * @return padded @p s (or @p s if already ≥ width).
 * @complexity O(n + width).
 * @alloc allocates.
 * @test CheatahString.Padding
 * @crtest StringCompileRun.Ljust
 * @systest StdlibE2E.String
 */
std::string ljust(std::string_view s, std::size_t width, std::string_view fill = " ");
/**
 * Right-justify to @p width.
 *
 * Pads @p s on the left with the fill character up to @p width; if @p s is already
 * at least @p width long it is returned unchanged. Only the first character of
 * @p fill is used (an empty @p fill defaults to a space).
 * @param s input.
 * @param width target.
 * @param fill pad char.
 * @return padded @p s.
 * @complexity O(n + width).
 * @alloc allocates the result plus concatenation temporaries.
 * @test CheatahString.Padding
 * @crtest StringCompileRun.Rjust
 * @systest StdlibE2E.String
 */
std::string rjust(std::string_view s, std::size_t width, std::string_view fill = " ");
/**
 * Center within @p width.
 *
 * Pads both sides with the fill character; when the padding is odd the extra
 * character goes on the right. Returns @p s unchanged if it is already at least
 * @p width long, and only the first character of @p fill is used (empty → space).
 * @param s input.
 * @param width target.
 * @param fill pad char.
 * @return padded @p s.
 * @complexity O(n + width).
 * @alloc allocates the result plus concatenation temporaries.
 * @test CheatahString.Padding
 * @crtest StringCompileRun.Center
 * @systest StdlibE2E.String
 */
std::string center(std::string_view s, std::size_t width, std::string_view fill = " ");
/**
 * Zero-fill on the left to @p width.
 *
 * Left-pads with `'0'` to @p width; if @p s begins with a `'+'` or `'-'` sign the
 * zeros are inserted after the sign. Returns @p s unchanged if already at least
 * @p width long.
 * @param s input.
 * @param width target.
 * @return `'0'`-padded @p s.
 * @complexity O(n + width).
 * @alloc allocates the result plus concatenation temporaries.
 * @test CheatahString.Padding
 * @crtest StringCompileRun.Zfill
 * @systest StdlibE2E.String
 */
std::string zfill(std::string_view s, std::size_t width);

// ---- whole-string classification (False for the empty string, like Python) ----
/**
 * All digits?
 *
 * True only if @p s is non-empty and every character is an ASCII decimal digit;
 * the empty string returns false (matching Python).
 * @param s input.
 * @return true iff non-empty and all `0–9`.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.Classification
 * @crtest StringCompileRun.Isdigit
 * @systest StdlibE2E.String
 */
bool isdigit(std::string_view s);
/**
 * All letters?
 *
 * True only if @p s is non-empty and every character is an ASCII letter (`std::isalpha`);
 * the empty string returns false.
 * @param s input.
 * @return true iff non-empty and all alphabetic.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.Classification
 * @crtest StringCompileRun.Isalpha
 * @systest StdlibE2E.String
 */
bool isalpha(std::string_view s);
/**
 * All alphanumeric?
 *
 * True only if @p s is non-empty and every character is an ASCII letter or digit
 * (`std::isalnum`); the empty string returns false.
 * @param s input.
 * @return true iff non-empty and all letters/digits.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.ClassificationAlnumAndSpace
 * @crtest StringCompileRun.Isalnum
 * @systest StdlibE2E.String
 */
bool isalnum(std::string_view s);
/**
 * All whitespace?
 *
 * True only if @p s is non-empty and every character is ASCII whitespace
 * (`std::isspace`: space, tab, newline, CR, form-feed, vertical tab); the empty
 * string returns false.
 * @param s input.
 * @return true iff non-empty and all whitespace.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.ClassificationAlnumAndSpace
 * @crtest StringCompileRun.Isspace
 * @systest StdlibE2E.String
 */
bool isspace(std::string_view s);
/**
 * All uppercase?
 *
 * True iff @p s contains at least one ASCII uppercase letter and no lowercase letters;
 * non-letter characters are ignored, so e.g. "ABC123" is uppercase but "123" and the
 * empty string are not.
 * @param s input.
 * @return true iff @p s has ≥ 1 uppercase letter and no lowercase.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.Classification
 * @crtest StringCompileRun.Isupper
 * @systest StdlibE2E.String
 */
bool isupper(std::string_view s);
/**
 * All lowercase?
 *
 * True iff @p s contains at least one ASCII lowercase letter and no uppercase letters;
 * non-letter characters are ignored, so e.g. "abc123" is lowercase but "123" and the
 * empty string are not.
 * @param s input.
 * @return true iff @p s has ≥ 1 lowercase letter and no uppercase.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahString.Classification
 * @crtest StringCompileRun.Islower
 * @systest StdlibE2E.String
 */
bool islower(std::string_view s);

} // namespace cheatah::string
