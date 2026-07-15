// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file builtins.hpp
 * @brief cheatah `builtins` — Python's always-available built-ins (no `import`):
 *        length, character/representation conversions, and hashing.
 *
 * The compiler auto-includes this header and resolves bare calls like `len("x")`
 * to `builtins::len`. The mathematical built-ins (`abs`/`min`/`max`/`round`/`pow`)
 * live in the `math` module. Unit tests: `stdlib/tests/builtins_test.cpp`; the
 * suite runs under AddressSanitizer (the `asan` preset) and Valgrind
 * (`security/run-valgrind.sh`) on every QA-gate run.
 */
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cheatah::builtins {

/// Sized<C>: C reports a `.size()` — strings and STL containers (list/dict/array).
template <typename C>
concept Sized = requires(const C& c) {
    { c.size() } -> std::convertible_to<std::size_t>;
};

/// Value<T>: a cheatah value — movable, so it can be stored, passed, and returned.
/// This is the baseline concept purrc stamps on every emitted function/method
/// parameter, so no cheatah code ever instantiates a fully unconstrained template
/// (keeps compile errors comprehensible). See `constrain-all-templates` policy.
template <typename T>
concept Value = std::movable<std::remove_cvref_t<T>>;

/**
 * Length / element count.
 *
 * Forwards to the container's `.size()`; for strings this is the byte length,
 * not a Unicode code-point count.
 * @param c a string or sized container.
 * @return `c.size()`.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.LenOrdChr
 * @crtest BuiltinsCompileRun.Len
 * @systest StdlibE2E.Builtins
 */
template <Sized C>
std::size_t len(const C& c) { return c.size(); }
/**
 * Length of a C-string / string literal.
 *
 * Returns the byte length of the view; any embedded NUL bytes are counted (the
 * length comes from the view, not from a terminating NUL).
 * @param s the string.
 * @return its byte length.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.LenOrdChr
 * @crtest BuiltinsCompileRun.Len
 * @systest StdlibE2E.Builtins
 */
std::size_t len(std::string_view s);

/**
 * Code point of the first byte.
 *
 * Returns the unsigned value of `s[0]` (0–255), ignoring trailing bytes;
 * an empty string yields 0 rather than throwing.
 * @param s a one-character string.
 * @return its byte value (0 if empty).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.LenOrdChr
 * @crtest BuiltinsCompileRun.Ord
 * @systest StdlibE2E.Builtins
 */
int ord(std::string_view s);

/**
 * ord() of a single char — what iterating a string yields (`for ch in s`), so ord(ch)
 * works inside such loops.
 * @param c the character (a single byte).
 * @return its unsigned byte value (0–255).
 * @complexity O(1). @alloc none. @test CheatahBuiltins.Ord
 * @crtest LangFeatures.Modulo
 */
constexpr int ord(char c) { return static_cast<unsigned char>(c); }
/**
 * Character for a code point.
 *
 * Builds a one-byte string from the low 8 bits of @p codepoint (it is narrowed
 * to `char`), so values outside 0–255 wrap modulo 256 rather than producing
 * multi-byte output.
 * @param codepoint a byte value.
 * @return the one-character string.
 * @complexity O(1).
 * @alloc none (1-char small-string optimization).
 * @test CheatahBuiltins.LenOrdChr
 * @crtest BuiltinsCompileRun.Chr
 * @systest StdlibE2E.Builtins
 */
std::string chr(int codepoint);

/**
 * Hex representation.
 *
 * Formats @p value in base 16 with lowercase digits and a `0x` prefix; negatives
 * are rendered as a leading `-` before the prefix (e.g. `-0x1f`), and 0 is `0x0`.
 * @param value the integer.
 * @return `"0x…"` (with sign).
 * @complexity O(log @p value).
 * @alloc allocates the result string.
 * @test CheatahBuiltins.BaseReprs
 * @crtest BuiltinsCompileRun.Hex
 * @systest StdlibE2E.Builtins
 */
std::string hex(long long value);
/**
 * Octal representation.
 *
 * Formats @p value in base 8 with a `0o` prefix; negatives get a leading `-`
 * before the prefix (e.g. `-0o17`), and 0 is `0o0`.
 * @param value the integer.
 * @return `"0o…"` (with sign).
 * @complexity O(log @p value).
 * @alloc allocates the result string.
 * @test CheatahBuiltins.BaseReprs
 * @crtest BuiltinsCompileRun.Oct
 * @systest StdlibE2E.Builtins
 */
std::string oct(long long value);
/**
 * Binary representation.
 *
 * Formats @p value in base 2 with a `0b` prefix; negatives get a leading `-`
 * before the prefix (e.g. `-0b101`), and 0 is `0b0`.
 * @param value the integer.
 * @return `"0b…"` (with sign).
 * @complexity O(log @p value).
 * @alloc allocates the result string.
 * @test CheatahBuiltins.BaseReprs
 * @crtest BuiltinsCompileRun.Bin
 * @systest StdlibE2E.Builtins
 */
std::string bin(long long value);

/**
 * Printable-ASCII repr (non-printables/`\`/`'` escaped, single-quoted).
 *
 * Wraps @p s in single quotes, passing through printable ASCII (bytes 32–126)
 * verbatim while escaping `\` and `'` as `\\`/`\'` and emitting any other byte as
 * a two-digit `\xHH` hex escape.
 * @param s input.
 * @return the quoted repr.
 * @complexity O(n).
 * @alloc allocates the result string.
 * @test CheatahBuiltins.Ascii
 * @crtest BuiltinsCompileRun.Ascii
 * @systest StdlibE2E.Builtins
 */
std::string ascii(std::string_view s);

/**
 * Truthiness of a string.
 *
 * Truthy iff non-empty; a whitespace-only or `"0"`/`"false"` string is still
 * truthy (only emptiness is false).
 * @param s input.
 * @return false iff @p s is empty.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Conversions
 * @crtest BuiltinsCompileRun.BoolFromString
 * @systest StdlibE2E.Builtins
 */
bool to_bool(std::string_view s);
/**
 * Truthiness of a number.
 * @param x any arithmetic value.
 * @return @p x != 0.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Conversions
 * @crtest BuiltinsCompileRun.BoolFromNonzero
 * @systest StdlibE2E.Builtins
 */
template <typename T>
    requires std::is_arithmetic_v<T>
bool to_bool(T x) { return x != T{}; }
/**
 * Parse a base-10 integer.
 *
 * Parses leading whitespace and an optional sign followed by decimal digits via
 * `std::stoll`; it stops at the first non-digit (so trailing junk is ignored),
 * throws on no parseable digits, and throws on out-of-range values.
 * @param s the integer string.
 * @return its value (throws on bad input).
 * @complexity O(n).
 * @alloc allocates a temporary `std::string` for the parse.
 * @test CheatahBuiltins.Conversions
 * @crtest BuiltinsCompileRun.IntFromString
 * @systest StdlibE2E.Builtins
 */
long long to_int(std::string_view s);
/**
 * Truncate a double to an integer.
 *
 * Truncates toward zero (drops the fractional part rather than rounding), so
 * `2.9` becomes 2 and `-2.9` becomes -2; values outside `long long` range are
 * undefined behavior.
 * @param x the value.
 * @return @p x toward zero.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Conversions
 * @crtest BuiltinsCompileRun.IntFromFloat
 * @systest StdlibE2E.Builtins
 */
long long to_int(double x);
/**
 * Parse a float.
 *
 * Parses leading whitespace and a floating-point literal via `std::stod`,
 * accepting decimal, scientific (`1e9`), `inf`, and `nan` forms; it stops at the
 * first unparsed character, throws when nothing parses, and throws on overflow.
 * @param s a floating-point string.
 * @return its value (throws on bad input).
 * @complexity O(n).
 * @alloc allocates a temporary `std::string` for the parse.
 * @test CheatahBuiltins.Conversions
 * @crtest BuiltinsCompileRun.FloatFromString
 * @systest StdlibE2E.Builtins
 */
double to_float(std::string_view s);
/// Number: any built-in arithmetic type — every width float() accepts numerically.
template <typename T>
concept Number = std::is_arithmetic_v<T>;
/**
 * `float()` of any NUMBER — one widening/identity conversion for every arithmetic type, so
 * overload resolution can never route a `double` (or an `i32`) through an integer overload
 * and silently TRUNCATE: `float(0.95)` must be 0.95, never 0.
 * @tparam T the arithmetic source type (`Number`).
 * @param x the value.
 * @return @p x as a `double`.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.ToFloatFromInt
 * @test CheatahBuiltins.ToFloatFromFloat
 * @crtest BuiltinsCompileRun.FloatFromInt
 * @crtest BuiltinsCompileRun.FloatFromFloat
 * @systest StdlibE2E.Builtins
 */
template <Number T>
constexpr double to_float(T x) { return static_cast<double>(x); }

/// Streamable<T>: T can be written to a `std::ostream` with `operator<<` — the requirement
/// for str() to render it. (Mirrors io's Printable, so bare `str(x)` and `io.str(x)` agree.)
template <typename T>
concept Streamable = requires(std::ostream& os, const T& v) {
    { os << v } -> std::convertible_to<std::ostream&>;
};

/**
 * Python `str()`: stringify any streamable value (an always-available built-in, so it needs
 * no `import` — bare `str(x)` resolves here, like `int()`/`float()`/`bool()`).
 *
 * Renders @p value via its `operator<<` into a fresh `ostringstream`, so the text matches
 * whatever that stream insertion produces (e.g. default 6-significant-digit float precision),
 * agreeing with `io.print`/`io.str`.
 * @param value the value to render.
 * @return @p value formatted as text.
 * @complexity O(n) in the output length.
 * @alloc allocates the result string (via an ostringstream).
 * @test CheatahBuiltins.Str
 * @crtest BuiltinsCompileRun.Str
 * @systest StdlibE2E.Builtins
 */
template <Streamable T>
std::string str(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

/**
 * `str()` for a bool — Python's capitalized spelling.
 *
 * Overrides the default streaming of a bool (`1`/`0`) to emit `True`/`False`, matching
 * `io.str` and `io.print`.
 * @param b the boolean.
 * @return `"True"` or `"False"`.
 * @complexity O(1).
 * @alloc allocates the small result string.
 * @test CheatahBuiltins.Str
 * @crtest BuiltinsCompileRun.Str
 * @systest StdlibE2E.Builtins
 */
inline std::string str(bool b) { return b ? "True" : "False"; }

/**
 * `str()` for the byte-width integers `i8`/`u8` (`std::int8_t`/`std::uint8_t`, which are
 * typedefs of `signed char`/`unsigned char`). Streaming a `char`-sized type would print a
 * CHARACTER; these overloads promote to a wider integer first so `i8`/`u8` render as NUMBERS —
 * the one seam through which `repr`, `print`, and container `str`/`repr` all inherit the fix.
 * Plain `char` is a distinct type (cheatah has no bare-`char` value type — single chars are
 * 1-char `std::string`), so it is deliberately not matched here.
 * @param v the `i8` value to render.
 * @return the value's decimal digits.
 * @complexity O(1).
 * @alloc allocates the small result string.
 */
inline std::string str(signed char v) { return std::to_string(static_cast<int>(v)); }
/**
 * `str()` for `u8` (`std::uint8_t`) — numeric, not a character. See @ref str(signed char).
 * @param v the `u8` value to render.
 * @return the value's decimal digits.
 */
inline std::string str(unsigned char v) { return std::to_string(static_cast<unsigned>(v)); }

/**
 * True division — the cheatah `/` operator (like Python 3): **always floating-point**,
 * so `6 / 2` is `3.0`, not `3`, and integer operands never silently truncate. Use the
 * `//` operator (@ref floordiv) when you want integer/floor division.
 * @param a numerator.
 * @param b denominator.
 * @return `double(a) / double(b)`.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Division
 * @crtest BuiltinsCompileRun.TrueDivision
 * @systest StdlibE2E.Builtins
 */
template <typename A, typename B>
    requires std::is_arithmetic_v<A> && std::is_arithmetic_v<B>
double truediv(A a, B b) {
    return static_cast<double>(a) / static_cast<double>(b);
}
/**
 * Floor division — the cheatah `//` operator (like Python): the quotient floored toward
 * −∞. Integer operands give an integer (`7 // 2 == 3`, `-7 // 2 == -4`, flooring the way
 * Python does, not truncating toward zero like raw C++); a floating operand gives a
 * floored double (`7.0 // 2 == 3.0`).
 * @param a numerator.
 * @param b denominator.
 * @return `floor(a / b)`, integral for integral operands.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Division
 * @crtest BuiltinsCompileRun.FloorDivision
 * @systest StdlibE2E.Builtins
 */
template <std::integral A, std::integral B>
std::common_type_t<A, B> floordiv(A a, B b) {
    // A controlled error, not UB: integer divide-by-zero is undefined in C++ (SIGFPE/trap), so guard
    // it so pure-cheatah `x // 0` raises rather than corrupting the process. (Float `//` is IEEE-safe.)
    if (b == 0) throw std::domain_error("integer floor division by zero");
    std::common_type_t<A, B> q = a / b;                 // C++ truncates toward zero…
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;      // …adjust to floor toward −∞
    return q;
}
/**
 * Floor division (`//`) for floating-point operands: floors the quotient toward −∞.
 *
 * Selected when at least one operand is floating-point (the all-integer case uses the
 * @ref floordiv overload above). Mirrors Python, where `7.0 // 2.0 == 3.0`.
 * @param a numerator.
 * @param b denominator.
 * @return `std::floor(double(a) / double(b))`.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Division
 * @crtest BuiltinsCompileRun.FloorDivision
 * @systest StdlibE2E.Builtins
 */
template <typename A, typename B>
    requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> &&
             !(std::integral<A> && std::integral<B>))
double floordiv(A a, B b) {
    return std::floor(static_cast<double>(a) / static_cast<double>(b));
}

/**
 * Content hash of a string.
 *
 * Hashes the bytes via `std::hash<std::string_view>` (equal contents hash
 * equally); the value is implementation-defined and unstable across runs and
 * compilers (do not persist it).
 * @param s input.
 * @return a `std::size_t` hash.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahBuiltins.Hash
 * @systest StdlibE2E.Builtins
 * @note No @crtest: compile-run coverage is intentionally skipped because the
 *       hash value is implementation-defined and has no portable expected stdout.
 */
std::size_t hash(std::string_view s);
/**
 * Hash of any hashable value.
 *
 * Defers to `std::hash<T>` for the static type of @p x, so it requires a
 * specialization to exist for `T`; like the string overload, the result is
 * implementation-defined and unstable across runs.
 * @param x the value.
 * @return `std::hash<T>{}(x)`.
 * @complexity O(1) for scalars.
 * @alloc none.
 * @test CheatahBuiltins.Hash
 * @systest StdlibE2E.Builtins
 * @note No @crtest: compile-run coverage is intentionally skipped because the
 *       hash value is implementation-defined and has no portable expected stdout.
 */
template <typename T>
    requires requires(const T& x) { std::hash<T>{}(x); }
std::size_t hash(const T& x) { return std::hash<T>{}(x); }

// ---- collection + method-style helpers ----
//
// These power cheatah's growable lists and the method-call syntax `obj.f(a)`,
// which lowers to `cheatah::builtins::f(obj, a)`. Free-function form works too:
// `append(xs, x)` and `xs.append(x)` are the same call.

/**
 * Append @p x to list @p v in place (Python `list.append`).
 *
 * Grows @p v by one, converting @p x to the list's element type. Usable as a
 * method (`xs.append(x)`) or a bare call (`append(xs, x)`); the list is taken by
 * reference, so the caller's list is mutated.
 * @param v the list to grow.
 * @param x the value to append.
 * @complexity amortized O(1).
 * @alloc reallocates @p v when it outgrows its capacity.
 * @test CheatahBuiltins.Append
 * @crtest BuiltinsCompileRun.Append
 * @systest StdlibE2E.Builtins
 */
template <typename T, typename U>
    requires std::convertible_to<std::remove_cvref_t<U>, T>
void append(std::vector<T>& v, U&& x) {
    v.push_back(static_cast<T>(std::forward<U>(x)));
}

/**
 * Whether @p s begins with @p prefix (Python `str.startswith`).
 * @param s the string.
 * @param prefix the prefix to test.
 * @return true iff @p s starts with @p prefix.
 * @complexity O(len(@p prefix)).
 * @alloc none.
 * @test CheatahBuiltins.StringPredicates
 * @crtest BuiltinsCompileRun.StartsWith
 * @systest StdlibE2E.Builtins
 */
inline bool startswith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/**
 * Whether @p s ends with @p suffix (Python `str.endswith`).
 * @param s the string.
 * @param suffix the suffix to test.
 * @return true iff @p s ends with @p suffix.
 * @complexity O(len(@p suffix)).
 * @alloc none.
 * @test CheatahBuiltins.StringPredicates
 * @crtest BuiltinsCompileRun.EndsWith
 * @systest StdlibE2E.Builtins
 */
inline bool endswith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * Whether @p sub occurs anywhere in @p s (Python `sub in s`).
 * @param s the string to search.
 * @param sub the substring to find.
 * @return true iff @p sub is a substring of @p s.
 * @complexity O(len(@p s) · len(@p sub)) worst case.
 * @alloc none.
 * @test CheatahBuiltins.StringPredicates
 * @crtest BuiltinsCompileRun.Contains
 * @systest StdlibE2E.Builtins
 */
inline bool contains(std::string_view s, std::string_view sub) {
    return s.find(sub) != std::string_view::npos;
}

/**
 * Membership test for a dict: is @p key present? Backs the `in` operator (`k in d`).
 * @param d the dict to search.
 * @param key the key to look for.
 * @return true iff @p key is present in @p d.
 * @complexity O(1) average.
 * @alloc none.
 * @test CheatahBuiltins.ContainsDict
 * @crtest LangFeatures.InOperator
 */
template <class K, class V, class H, class E, class A, class Key>
bool contains(const std::unordered_map<K, V, H, E, A>& d, const Key& key) {
    return d.count(key) != 0;
}

/**
 * Membership test for a list: does any element equal @p value? Backs `x in xs`.
 * @param xs the list to scan.
 * @param value the value to look for.
 * @return true iff some element of @p xs compares equal to @p value.
 * @complexity O(n).
 * @alloc none.
 * @test CheatahBuiltins.ContainsList
 * @crtest LangFeatures.InOperator
 */
template <class T, class A, class Value>
bool contains(const std::vector<T, A>& xs, const Value& value) {
    for (const T& x : xs) {
        if (x == value) return true;
    }
    return false;
}

/**
 * Python FLOOR-mod for integers: the result takes the DIVISOR's sign (-7 % 3 == 2), unlike
 * raw C++ `%`. Backs the `%` operator.
 * @param a the dividend.
 * @param b the divisor; @p b == 0 throws std::domain_error (integer modulo by zero).
 * @return a mod b with the sign of @p b (Python floor-mod semantics).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Mod
 * @crtest LangFeatures.Modulo
 */
template <class A, class B>
    requires(std::is_integral_v<A> && std::is_integral_v<B>)
std::common_type_t<A, B> mod(A a, B b) {
    using R = std::common_type_t<A, B>;
    // A controlled error, not UB: integer `% 0` is undefined in C++ (SIGFPE/trap) — guard it so
    // pure-cheatah `x % 0` raises rather than corrupting the process. (Float `%` is IEEE-safe.)
    if (b == 0) throw std::domain_error("integer modulo by zero");
    const R r = static_cast<R>(a) % static_cast<R>(b);
    return (r != 0 && ((r < 0) != (b < 0))) ? r + static_cast<R>(b) : r;
}

/**
 * Python floor-mod for floats (either operand): fmod adjusted to the divisor's sign,
 * mirroring `7.5 % 2 == 1.5` and `-7.5 % 2 == 0.5`.
 * @param a the dividend.
 * @param b the divisor.
 * @return a mod b as a double, with the sign of @p b (Python floor-mod semantics).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Mod
 * @crtest LangFeatures.Modulo
 */
template <class A, class B>
    requires(!std::is_integral_v<A> || !std::is_integral_v<B>)
double mod(A a, B b) {
    const double r = std::fmod(static_cast<double>(a), static_cast<double>(b));
    return (r != 0.0 && ((r < 0.0) != (static_cast<double>(b) < 0.0))) ? r + static_cast<double>(b)
                                                                       : r;
}

// ---- indexing & slicing (Python `seq[i]` / `seq[i:j]`) ----
//
// The compiler lowers value-position `seq[i]` to `index(seq, i)` and `seq[a:b]`
// to `slice(seq, a, b)` (a missing bound becomes 0 / `slice_end`). Indices may be
// negative (counted from the end). Indexing a string yields a length-1 string
// (Python semantics), so `s[i] == "<"` type-checks; slicing yields the same kind.

/// Sentinel for an omitted slice upper bound (`s[a:]`): "to the end".
inline constexpr long long slice_end = std::numeric_limits<long long>::max();

namespace detail {
inline long long norm_index(long long i, long long n) { return i < 0 ? i + n : i; }
}  // namespace detail

/**
 * Element at @p i of a string — a length-1 string (Python `s[i]`).
 * Negative @p i counts from the end; out-of-range throws `std::out_of_range`.
 * @param s the string.
 * @param i the index (may be negative).
 * @return the one-character string at @p i.
 * @complexity O(1).
 * @alloc none (1-char small-string optimization).
 * @test CheatahBuiltins.IndexString
 * @crtest BuiltinsCompileRun.IndexString
 * @systest StdlibE2E.Builtins
 */
inline std::string index(const std::string& s, long long i) {
    const long long n = static_cast<long long>(s.size());
    i = detail::norm_index(i, n);
    if (i < 0 || i >= n) throw std::out_of_range("string index out of range");
    return std::string(1, s[static_cast<std::size_t>(i)]);
}

/**
 * Element at @p i of a list/array (Python `xs[i]`), by value.
 * Negative @p i counts from the end; out-of-range throws `std::out_of_range`.
 * @param c the sequence.
 * @param i the index (may be negative).
 * @return a copy of the element at @p i.
 * @complexity O(1).
 * @alloc none beyond copying the element.
 * @test CheatahBuiltins.IndexList
 * @crtest BuiltinsCompileRun.IndexList
 * @systest StdlibE2E.Builtins
 */
template <typename C>
    requires requires(const C& c) { c.data(); c.size(); }  // contiguous seq (vector/array), not a map
auto index(const C& c, long long i) -> std::decay_t<decltype(c[0])> {
    const long long n = static_cast<long long>(c.size());
    i = detail::norm_index(i, n);
    if (i < 0 || i >= n) throw std::out_of_range("index out of range");
    return c[static_cast<std::size_t>(i)];
}

/**
 * Element at @p i of a `list[bool]` (Python `xs[i]`), by value.
 * `std::vector<bool>` is the one sequence type the contiguous overload above
 * cannot accept: it is bit-packed, so it has proxy references and no `.data()`.
 * Same semantics — negative @p i counts from the end; out-of-range throws.
 * @param c the bool list.
 * @param i the index (may be negative).
 * @return the element at @p i.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.IndexBoolList
 * @crtest BuiltinsCompileRun.IndexBoolList
 * @systest StdlibE2E.Builtins
 */
inline bool index(const std::vector<bool>& c, long long i) {
    const long long n = static_cast<long long>(c.size());
    i = detail::norm_index(i, n);
    if (i < 0 || i >= n) throw std::out_of_range("index out of range");
    return c[static_cast<std::size_t>(i)];
}

/**
 * Value for @p key in a dict (Python `d[key]`), by value.
 * @param m the dict.
 * @param key the key to look up.
 * @return a copy of the mapped value (throws `std::out_of_range` if absent).
 * @complexity O(1) average.
 * @alloc none beyond copying the value.
 * @test CheatahBuiltins.IndexDict
 * @crtest BuiltinsCompileRun.IndexDict
 * @systest StdlibE2E.Builtins
 */
template <typename K, typename V, typename H, typename E, typename A, typename Key>
    requires requires(const std::unordered_map<K, V, H, E, A>& m, const Key& key) { m.find(key); }
V index(const std::unordered_map<K, V, H, E, A>& m, const Key& key) {
    const auto it = m.find(key);
    if (it == m.end()) throw std::out_of_range("key not found");
    return it->second;
}

/**
 * Substring `s[lo:hi]` (Python slice semantics: clamped, negatives from the end).
 * @param s the string.
 * @param lo start index (default 0 at the call site).
 * @param hi end index, or @ref slice_end for "to the end".
 * @return the slice (empty if `lo >= hi` after clamping).
 * @complexity O(hi-lo).
 * @alloc the result string.
 * @test CheatahBuiltins.SliceString
 * @crtest BuiltinsCompileRun.SliceString
 * @systest StdlibE2E.Builtins
 */
inline std::string slice(const std::string& s, long long lo, long long hi) {
    const long long n = static_cast<long long>(s.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    if (lo >= hi) return std::string();
    return s.substr(static_cast<std::size_t>(lo), static_cast<std::size_t>(hi - lo));
}

/**
 * Sub-list `xs[lo:hi]` (Python slice semantics), returned as a new list.
 * @param c the sequence.
 * @param lo start index.
 * @param hi end index, or @ref slice_end for "to the end".
 * @return the slice as a `std::vector` of the element type.
 * @complexity O(hi-lo).
 * @alloc the result vector.
 * @test CheatahBuiltins.SliceList
 * @crtest BuiltinsCompileRun.SliceList
 * @systest StdlibE2E.Builtins
 */
template <typename C>
    requires requires(const C& c) { c.data(); c.size(); }  // contiguous seq, not a map
auto slice(const C& c, long long lo, long long hi) -> std::vector<std::decay_t<decltype(c[0])>> {
    const long long n = static_cast<long long>(c.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    std::vector<std::decay_t<decltype(c[0])>> out;
    for (long long k = lo; k < hi; ++k) out.push_back(c[static_cast<std::size_t>(k)]);
    return out;
}

} // namespace cheatah::builtins
