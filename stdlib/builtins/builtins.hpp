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
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
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
 * Returns the element count of any `Sized` container by forwarding to its
 * `.size()`; for strings this is the byte length, not a Unicode code-point count.
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
 * Returns the unsigned byte value of `s[0]` (range 0–255), ignoring any
 * trailing bytes; an empty string yields 0 rather than throwing.
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
 * A string is truthy when it has at least one byte; a whitespace-only or
 * `"0"`/`"false"` string is still truthy (only emptiness is false).
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
/**
 * Widen an integer to a double.
 * @param x the value.
 * @return @p x as a `double`.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.ToFloatFromInt
 * @crtest BuiltinsCompileRun.FloatFromInt
 * @systest StdlibE2E.Builtins
 */
double to_float(long long x);

/**
 * Content hash of a string.
 *
 * Hashes the bytes via `std::hash<std::string_view>`, so equal contents hash
 * equally; the value is implementation-defined and not stable across runs or
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
 * @alloc the 1-char result (small-string optimized).
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
