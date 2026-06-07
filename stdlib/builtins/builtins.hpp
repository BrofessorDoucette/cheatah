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
#include <string>
#include <string_view>
#include <type_traits>

namespace cheatah::builtins {

/// Sized<C>: C reports a `.size()` — strings and STL containers (list/dict/array).
template <typename C>
concept Sized = requires(const C& c) {
    { c.size() } -> std::convertible_to<std::size_t>;
};

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
std::size_t hash(const T& x) { return std::hash<T>{}(x); }

} // namespace cheatah::builtins
