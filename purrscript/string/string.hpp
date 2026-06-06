#pragma once

// purrscript string — common text operations + the Python `string` module
// constants. Mirrors https://docs.python.org/3/library/string.html plus the
// everyday str methods Python exposes (split/join/strip/replace/...), surfaced as
// free functions so a .purr program writes string.upper("meow").
//
// A standard-library MODULE: `import string` includes this header and links the
// string library (libcheatah_purrscript_string). Constants are header-only; the
// operations are compiled into the library; `join` is templated (header).
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::purrscript::string {

// ---- constants (Python `string` module) ----
inline constexpr std::string_view ascii_lowercase = "abcdefghijklmnopqrstuvwxyz";
inline constexpr std::string_view ascii_uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
inline constexpr std::string_view ascii_letters =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
inline constexpr std::string_view digits = "0123456789";
inline constexpr std::string_view hexdigits = "0123456789abcdefABCDEF";
inline constexpr std::string_view octdigits = "01234567";
inline constexpr std::string_view punctuation = R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";
inline constexpr std::string_view whitespace = " \t\n\r\f\v";

// ---- case ----
std::string upper(std::string_view s);
std::string lower(std::string_view s);
std::string capitalize(std::string_view s);  // first char upper, rest lower
std::string title(std::string_view s);        // Capitalize Each Word
std::string swapcase(std::string_view s);

// ---- trimming (default: ASCII whitespace) ----
std::string strip(std::string_view s, std::string_view chars = whitespace);
std::string lstrip(std::string_view s, std::string_view chars = whitespace);
std::string rstrip(std::string_view s, std::string_view chars = whitespace);

// ---- search / test ----
bool startswith(std::string_view s, std::string_view prefix);
bool endswith(std::string_view s, std::string_view suffix);
bool contains(std::string_view s, std::string_view sub);
long find(std::string_view s, std::string_view sub);   // first index, or -1
long rfind(std::string_view s, std::string_view sub);  // last index, or -1
long count(std::string_view s, std::string_view sub);  // non-overlapping count

// ---- transform ----
std::string replace(std::string_view s, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view s, std::string_view sep);
std::vector<std::string> split(std::string_view s);  // on runs of whitespace
std::vector<std::string> splitlines(std::string_view s);
std::string capwords(std::string_view s);  // Python string.capwords

// StringViewable<T>: a std::string_view can be built from T — what join() needs.
template <typename T>
concept StringViewable = requires(const T& v) { std::string_view(v); };

// join(sep, parts): concatenate string-like parts with `sep`. Templated over any
// range whose elements convert to string_view.
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
std::string ljust(std::string_view s, std::size_t width, std::string_view fill = " ");
std::string rjust(std::string_view s, std::size_t width, std::string_view fill = " ");
std::string center(std::string_view s, std::size_t width, std::string_view fill = " ");
std::string zfill(std::string_view s, std::size_t width);  // left-pad with '0'

// ---- whole-string classification (False for the empty string, like Python) ----
bool isdigit(std::string_view s);
bool isalpha(std::string_view s);
bool isalnum(std::string_view s);
bool isspace(std::string_view s);
bool isupper(std::string_view s);
bool islower(std::string_view s);

} // namespace cheatah::purrscript::string
