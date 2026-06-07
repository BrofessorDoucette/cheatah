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

/** Length / element count. @param c a string or sized container. @return `c.size()`.
 *  @note O(1); no heap allocation. @test CheatahBuiltins.LenOrdChr */
template <Sized C>
std::size_t len(const C& c) { return c.size(); }
/** Length of a C-string / string literal. @param s the string. @return its byte length.
 *  @note O(1); no heap. @test CheatahBuiltins.LenOrdChr */
std::size_t len(std::string_view s);

/** Code point of the first byte. @param s a one-character string. @return its byte value (0 if empty).
 *  @note O(1); no heap. @test CheatahBuiltins.LenOrdChr */
int ord(std::string_view s);
/** Character for a code point. @param codepoint a byte value. @return the one-character string.
 *  @note O(1); no heap (1-char small-string optimization). @test CheatahBuiltins.LenOrdChr */
std::string chr(int codepoint);

/** Hex representation. @param value the integer. @return `"0x…"` (with sign).
 *  @note O(log @p value); allocates the result string. @test CheatahBuiltins.BaseReprs */
std::string hex(long long value);
/** Octal representation. @param value the integer. @return `"0o…"` (with sign).
 *  @note O(log @p value); allocates the result string. @test CheatahBuiltins.BaseReprs */
std::string oct(long long value);
/** Binary representation. @param value the integer. @return `"0b…"` (with sign).
 *  @note O(log @p value); allocates the result string. @test CheatahBuiltins.BaseReprs */
std::string bin(long long value);

/** Printable-ASCII repr (non-printables/`\`/`'` escaped, single-quoted). @param s input.
 *  @return the quoted repr. @note O(n); allocates the result string. @test CheatahBuiltins.Ascii */
std::string ascii(std::string_view s);

/** Truthiness of a string. @param s input. @return false iff @p s is empty.
 *  @note O(1); no heap. @test CheatahBuiltins.Conversions */
bool to_bool(std::string_view s);
/** Truthiness of a number. @param x any arithmetic value. @return @p x != 0.
 *  @note O(1); no heap. @test CheatahBuiltins.Conversions */
template <typename T>
    requires std::is_arithmetic_v<T>
bool to_bool(T x) { return x != T{}; }
/** Parse a base-10 integer. @param s the integer string. @return its value (throws on bad input).
 *  @note O(n); allocates a temporary `std::string` for the parse. @test CheatahBuiltins.Conversions */
long long to_int(std::string_view s);
/** Truncate a double to an integer. @param x the value. @return @p x toward zero.
 *  @note O(1); no heap. @test CheatahBuiltins.Conversions */
long long to_int(double x);
/** Parse a float. @param s a floating-point string. @return its value (throws on bad input).
 *  @note O(n); allocates a temporary `std::string` for the parse. @test CheatahBuiltins.Conversions */
double to_float(std::string_view s);
/** Widen an integer to a double. @param x the value. @return @p x as a `double`.
 *  @note O(1); no heap. @test CheatahBuiltins.ToFloatFromInt */
double to_float(long long x);

/** Content hash of a string. @param s input. @return a `std::size_t` hash.
 *  @note O(n); no heap. @test CheatahBuiltins.Hash */
std::size_t hash(std::string_view s);
/** Hash of any hashable value. @param x the value. @return `std::hash<T>{}(x)`.
 *  @note O(1) for scalars; no heap. @test CheatahBuiltins.Hash */
template <typename T>
std::size_t hash(const T& x) { return std::hash<T>{}(x); }

} // namespace cheatah::builtins
