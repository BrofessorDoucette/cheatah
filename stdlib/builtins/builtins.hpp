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
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
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

// ---- errors -------------------------------------------------------------------------------------
//
// `raise` throws an Error and `except` catches one. An Error carries a KIND alongside its message, so a
// handler can select what it knows how to deal with (`except e of "index"`) and let everything else keep
// travelling — which is the whole difference between recovering from a failure and swallowing one.
//
// The kind is a plain string, not a class hierarchy, because cheatah has no inheritance: "is-a" is a
// concept, and a runtime taxonomy of errors is a discriminated value, not a base class. Kinds are open —
// any string works — so a library can name its own failures without every caller having to know them.
//
// An Error is still a `str` wherever one is expected: it converts and compares as its MESSAGE, so
// `io.print(e)` and `e == "boom"` read exactly as they did when a handler bound a bare string.

/// Conventional kinds raised from the language core. Libraries are free to define their own.
inline constexpr const char* kErrorKindError = "error";        ///< `raise "msg"` — unclassified
inline constexpr const char* kErrorKindIndex = "index";        ///< subscript out of range
inline constexpr const char* kErrorKindKey = "key";            ///< dict key absent
inline constexpr const char* kErrorKindArithmetic = "arithmetic";   ///< divide / modulo by zero
inline constexpr const char* kErrorKindUnknown = "unknown";    ///< a throw of a type we cannot inspect

/**
 * A raised error: a `kind` naming what went wrong and a human `message`.
 *
 * Derives from `std::runtime_error` so it interoperates with C++ code that catches `std::exception` —
 * that inheritance is a C++ implementation detail and is not visible from cheatah, where an Error is an
 * ordinary value with two string fields.
 */
class Error : public std::runtime_error {
public:
    /**
     * An unclassified error — what `raise "msg"` builds. Kind is @ref kErrorKindError.
     * @param message the human-readable description.
     * @complexity O(message).
     * @alloc copies the message (twice: the base class keeps its own).
     * @test CheatahBuiltins.ErrorCarriesKindAndMessage
     * @crtest PurrcPipeline.CompilesAndRunsTryExceptRaise
     */
    explicit Error(std::string message)
        : std::runtime_error(message), kind_(kErrorKindError), message_(std::move(message)) {}

    /**
     * A classified error — what `raise Error("kind", "msg")` builds.
     * @param kind the open-ended kind string a handler selects on (`except e of "kind"`).
     * @param message the human-readable description.
     * @complexity O(kind + message).
     * @alloc copies both strings.
     * @test CheatahBuiltins.ErrorCarriesKindAndMessage
     */
    Error(std::string kind, std::string message)
        : std::runtime_error(message), kind_(std::move(kind)), message_(std::move(message)) {}

    /**
     * @brief What CLASS of failure this is — the string `except … of` matches against.
     * @return the kind, by const reference; never empty for an Error built through these constructors.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahBuiltins.ErrorCarriesKindAndMessage
     */
    const std::string& kind() const noexcept { return kind_; }

    /**
     * @brief The human-readable description. This is also what @ref str and `operator<<` yield, so a
     *        caught error prints as its message rather than as a struct.
     * @return the message, by const reference.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahBuiltins.ErrorCarriesKindAndMessage
     */
    const std::string& message() const noexcept { return message_; }

    // Deliberately NOT implicitly convertible to std::string. It would read nicely, but `str()` is a
    // heavily overloaded set and an implicit conversion makes half of it ambiguous the moment an Error
    // is printed. The `str` overload and the comparisons below give the same ergonomics explicitly.

private:
    std::string kind_;
    std::string message_;
};

/**
 * @brief Compare an error against a string — by MESSAGE, so `e == "boom"` reads the way it did when a
 *        handler bound a bare string. Compare `e.kind()` when you mean the kind.
 * @param e the error.
 * @param s the message to compare against.
 * @return true when the error's message is exactly @p s.
 * @complexity O(min(len)).
 * @alloc none.
 * @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage
 */
inline bool operator==(const Error& e, const std::string& s) { return e.message() == s; }

/** @brief Message comparison, arguments reversed.
 *  @param s the message to compare against.
 *  @param e the error.
 *  @return true when the error's message is exactly @p s.
 *  @complexity O(min(len)). @alloc none.
 *  @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage */
inline bool operator==(const std::string& s, const Error& e) { return e.message() == s; }

/** @brief Message comparison against a string literal.
 *  @param e the error.
 *  @param s the NUL-terminated message to compare against.
 *  @return true when the error's message is exactly @p s.
 *  @complexity O(len(@p s)). @alloc none.
 *  @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage */
inline bool operator==(const Error& e, const char* s) { return e.message() == s; }

/** @brief Message comparison against a string literal, arguments reversed.
 *  @param s the NUL-terminated message to compare against.
 *  @param e the error.
 *  @return true when the error's message is exactly @p s.
 *  @complexity O(min(len)). @alloc none.
 *  @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage */
inline bool operator==(const char* s, const Error& e) { return e.message() == s; }

/** @brief Stream an error as its MESSAGE — the kind would be noise in output that wanted the sentence.
 *  @param os the destination stream.
 *  @param e the error.
 *  @return @p os, for chaining.
 *  @complexity O(message). @alloc none beyond the stream's own.
 *  @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage */
inline std::ostream& operator<<(std::ostream& os, const Error& e) { return os << e.message(); }

/**
 * The error currently being handled, normalized to an @ref Error.
 *
 * Called from inside a `catch (...)`, where `throw;` re-raises the in-flight exception so it can be
 * inspected by type. This is what lets ONE handler shape cover a raised Error, a `std::exception` from
 * any C++ library, and a throw of some type we have never heard of — the last of which used to travel
 * straight past every handler and abort the process.
 *
 * @return the in-flight exception as an Error: a raised Error verbatim, a `std::out_of_range` as kind
 *         "index", a `std::domain_error` as "arithmetic", any other `std::exception` as "error", and
 *         anything else as "unknown".
 * @complexity O(1) plus the message copy.
 * @alloc copies the kind and message.
 * @test CheatahBuiltins.CurrentErrorNormalizesEveryThrownType
 * @crtest PurrcPipeline.CompilesAndRunsTryExceptRaise
 */
inline Error current_error() {
    try {
        throw;
    } catch (const Error& e) {
        return e;
    } catch (const std::out_of_range& e) {
        return {kErrorKindIndex, e.what()};
    } catch (const std::domain_error& e) {
        return {kErrorKindArithmetic, e.what()};
    } catch (const std::exception& e) {
        return {kErrorKindError, e.what()};
    } catch (...) {
        return {kErrorKindUnknown, "unknown error"};
    }
}

/// Runs its action when the scope ends, however it ends — the body of a `finally`.
template <std::invocable F>
class Finally {
public:
    /**
     * @brief Take ownership of the action to run at scope exit.
     * @param f the callable to invoke from the destructor.
     * @complexity O(1).
     * @alloc moves @p f into the guard.
     * @test CheatahBuiltins.FinallyRunsOnEveryExitPath
     */
    explicit Finally(F f) : f_(std::move(f)) {}
    Finally(const Finally&) = delete;
    Finally& operator=(const Finally&) = delete;
    Finally(Finally&&) = delete;             // a guard is pinned to its scope — never transferred.
    Finally& operator=(Finally&&) = delete;

    /**
     * @brief Run the action. Reached on every exit path — normal fall-through, `return`, `break`, or an
     *        exception unwinding through the scope, which is the whole point of a guard over a
     *        duplicated block.
     * @complexity that of the action.
     * @alloc that of the action.
     * @test CheatahBuiltins.FinallyRunsOnEveryExitPath
     * @test CheatahBuiltins.FinallySwallowsItsOwnThrowDuringUnwinding
     */
    ~Finally() {
        // A `finally` that throws while an exception is already unwinding would terminate the process,
        // which is a worse outcome than losing the second error — so it is swallowed here.
        // (The handler is one line deliberately: Finally is a template, so an instantiation whose
        // action cannot throw leaves a standalone `}` that no test can ever reach. The swallow itself
        // IS covered — see CheatahBuiltins.FinallySwallowsItsOwnThrowDuringUnwinding.)
        try {
            f_();
        } catch (...) {}   // NOLINT(bugprone-empty-catch) — swallowing is the documented contract
    }

private:
    F f_;
};

/**
 * @brief Build a scope guard around @p f — how `finally { … }` lowers.
 * @param f the callable to run when the enclosing scope ends.
 * @return the guard; keep it alive for the scope you want covered.
 * @complexity O(1).
 * @alloc moves @p f into the returned guard.
 * @test CheatahBuiltins.FinallyRunsOnEveryExitPath
 */
template <std::invocable F>
Finally<F> make_finally(F f) {
    return Finally<F>(std::move(f));
}

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
 * @alloc allocates the result string, built from a temporary digits buffer.
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
 * @alloc allocates the result string, built from a temporary digits buffer.
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
 * @alloc allocates the result string, built from a temporary digits buffer.
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
 * @alloc allocates the result string, plus a temporary ostringstream per escaped byte.
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
 * @warning @p x outside `long long`'s range (or NaN) is undefined behavior — no clamp or check.
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
 * @systest StdlibE2E.String
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
 * `str()` of an error is its MESSAGE — printing a caught error says what went wrong, without the kind
 * turning up uninvited in output that only wanted the sentence. Reach for `.kind()` when you want it.
 * @param e the error to render.
 * @return the error's message.
 * @complexity O(message).
 * @alloc copies the message.
 * @test CheatahBuiltins.ErrorComparesAndPrintsAsItsMessage
 */
inline std::string str(const Error& e) { return e.message(); }

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
 * @test CheatahBuiltins.StrByteWidthIntsAreNumbers
 */
inline std::string str(signed char v) { return std::to_string(static_cast<int>(v)); }
/**
 * `str()` for `u8` (`std::uint8_t`) — numeric, not a character. See @ref str(signed char).
 * @param v the `u8` value to render.
 * @return the value's decimal digits.
 * @complexity O(1).
 * @alloc allocates the small result string.
 * @test CheatahBuiltins.StrByteWidthIntsAreNumbers
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
 * @systest StdlibE2E.Math
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
 * @param b denominator; @p b == 0 throws `std::domain_error` (integer floor division by zero).
 * @return `floor(a / b)`, integral for integral operands.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.Division
 * @test CheatahBuiltins.IntegerDivideAndModuloByZeroThrow
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
 * @note No `@crtest`: compile-run coverage is intentionally skipped because the
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
 * @note No `@crtest`: compile-run coverage is intentionally skipped because the
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
 * @crtest LangFeatures.AppendAndDictMutation
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
 * @crtest LangFeatures.MethodPredicates
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
 * @crtest LangFeatures.MethodPredicates
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
 * @crtest LangFeatures.MethodPredicates
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
 * @test CheatahBuiltins.IntegerDivideAndModuloByZeroThrow
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
 * @crtest LangFeatures.StringSlicingAndIndex
 * @systest StdlibE2E.Builtins
 */
inline std::string index(const std::string& s, long long i) {
    const auto n = static_cast<long long>(s.size());
    i = detail::norm_index(i, n);
    if (i < 0 || i >= n) throw std::out_of_range("string index out of range");
    std::string out(1, s[static_cast<std::size_t>(i)]);  // (1, c) must not become a braced list: {1, c} is two chars
    return out;
}

/**
 * Element at @p i of a list/array (Python `xs[i]`), by CONST REFERENCE.
 * Negative @p i counts from the end; out-of-range throws `std::out_of_range`.
 *
 * Returning a reference — not a copy — is what makes `xs[i].field` free: reading one field of a
 * heap-owning element (a struct with strings/lists) no longer deep-copies the whole element. Value
 * semantics are UNCHANGED at the `.purr` level, because codegen binds a subscript with plain `auto`
 * (`let e = xs[i]` still copies), and the const-ness preserves cheatah's "list elements are
 * read-only" rule — whole-element `xs[i] = v` assignment goes through a different path.
 *
 * Lifetime: the reference is into @p c, so it is valid as long as @p c is and is not mutated.
 * Subscripting a temporary container is safe in the expression that does it (the temporary outlives
 * the full-expression); binding that reference to a name that outlives the statement is not, and
 * codegen never emits such a binding.
 * @param c the sequence.
 * @param i the index (may be negative).
 * @return a const reference to the element at @p i.
 * @complexity O(1).
 * @alloc none.
 * @test CheatahBuiltins.IndexList
 * @crtest LangFeatures.ListSlicingAndIndex
 * @systest StdlibE2E.Builtins
 */
template <typename C>
    requires requires(const C& c) { c.data(); c.size(); }  // contiguous seq (vector/array), not a map
auto index(const C& c, long long i) -> const std::decay_t<decltype(c[0])>& {
    const auto n = static_cast<long long>(c.size());
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
    const auto n = static_cast<long long>(c.size());
    i = detail::norm_index(i, n);
    if (i < 0 || i >= n) throw std::out_of_range("index out of range");
    return c[static_cast<std::size_t>(i)];
}

/**
 * Value for @p key in a dict (Python `d[key]`), by CONST REFERENCE.
 * Same rationale and lifetime rules as the sequence overload above: `d[key].field` stops
 * deep-copying the mapped value, while `let v = d[key]` still copies.
 * @param m the dict.
 * @param key the key to look up.
 * @return a const reference to the mapped value; an absent key raises kind `"key"`, which is distinct
 *         from the `"index"` a sequence subscript raises — a missing dict entry and a walked-off-the-end
 *         list are different mistakes and a handler should be able to take one without the other.
 * @complexity O(1) average.
 * @alloc none.
 * @test CheatahBuiltins.IndexDict
 * @crtest LangFeatures.AppendAndDictMutation
 * @systest Callback.RichTypesThroughStdFunction
 */
template <typename K, typename V, typename H, typename E, typename A, typename Key>
    requires requires(const std::unordered_map<K, V, H, E, A>& m, const Key& key) { m.find(key); }
const V& index(const std::unordered_map<K, V, H, E, A>& m, const Key& key) {
    const auto it = m.find(key);
    if (it == m.end()) throw Error(kErrorKindKey, "key not found");
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
 * @crtest LangFeatures.StringSlicingAndIndex
 * @systest StdlibE2E.Ed25519
 */
inline std::string slice(const std::string& s, long long lo, long long hi) {
    const auto n = static_cast<long long>(s.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    if (lo >= hi) return {};
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
 * @crtest LangFeatures.ListSlicingAndIndex
 * @systest StdlibE2E.Builtins
 */
template <typename C>
    requires requires(const C& c) { c.data(); c.size(); }  // contiguous seq, not a map
auto slice(const C& c, long long lo, long long hi) -> std::vector<std::decay_t<decltype(c[0])>> {
    const auto n = static_cast<long long>(c.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    // assign() from the source range sizes the buffer ONCE — a push_back loop re-tests capacity every
    // iteration and reallocates O(log n) times, which also keeps the loop from ever vectorizing. For
    // trivially-copyable elements libstdc++ lowers this to a single memmove.
    // Clamping with a ternary rather than guarding the assign with an `if` keeps every line here
    // unconditionally executed (the coverage gate demands 100% lines), and assign() is happy with an
    // empty range, so a reversed lo/hi simply yields an empty list exactly as the old loop did.
    const long long stop = hi < lo ? lo : hi;
    std::vector<std::decay_t<decltype(c[0])>> out;
    out.assign(c.begin() + static_cast<std::size_t>(lo), c.begin() + static_cast<std::size_t>(stop));
    return out;
}

// ---- slice ASSIGNMENT ------------------------------------------------------------------
// `seq[lo:hi] = rhs` lowers to `slice_assign(seq, lo, hi, rhs)`. Bounds are normalised exactly as
// @ref slice does, so the write addresses the elements the matching read would have returned.
//
// Each container decides what an assignment MEANS, by overload resolution — the compiler has no
// type information to decide with. A list REPLACES the range and resizes (Python); ndarray and
// fixarray COPY the values into storage they already own (they are arrays: an assignment fills
// them, it never rebinds or resizes them); a string refuses, being immutable.

/**
 * The empty sequence on the right of a slice assignment (`xs[a:b] = []`).
 *
 * An empty list literal carries no element type, so it cannot be spelled as a `std::vector`
 * without one. The compiler emits this tag instead and each container decides what it means:
 * a list DELETES the range, while a fixed-extent array refuses, having nothing to shrink.
 */
struct empty_seq {};

// ndarray.hpp reopens this namespace without including this header, so it spells the same
// sentinel locally. If either value ever moves, this fails rather than silently disagreeing.
static_assert(slice_end == std::numeric_limits<long long>::max(),
              "builtins::slice_end and ndarray's nd_slice_end must hold the same value");

/**
 * Delete `v[lo:hi]` — the `xs[a:b] = []` form.
 * @tparam T the element type.
 * @param v the list to modify in place.
 * @param lo start index (negative counts from the end).
 * @param hi end index, or @ref slice_end for "to the end".
 * @complexity O(size) — the tail shifts down.
 * @alloc none — erasing never reallocates.
 * @test CheatahBuiltins.SliceAssignList
 * @crtest LangFeatures.ListSliceAssignment
 * @systest StdlibE2E.Builtins
 */
template <typename T>
void slice_assign(std::vector<T>& v, long long lo, long long hi, empty_seq /*empty*/) {
    const auto n = static_cast<long long>(v.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (lo > n) lo = n;
    if (hi > n) hi = n;
    if (hi < lo) hi = lo;
    v.erase(v.begin() + static_cast<std::size_t>(lo), v.begin() + static_cast<std::size_t>(hi));
}

/**
 * Write @p rhs into `v[lo:hi]` of a fixed-size `array<T, N>` — it is FILLED, never resized.
 *
 * The extent is part of the type, so a source of the wrong length is an error rather than a
 * partial write. Bounds follow @ref slice. This is the same contract `fixarray` and `ndarray`
 * keep: an assignment into an array copies values into storage the array already owns.
 * @tparam T the element type.
 * @tparam N the extent.
 * @tparam R the source range type.
 * @param v the array to write into.
 * @param lo start index (negative counts from the end).
 * @param hi end index, or @ref slice_end for "to the end".
 * @param rhs the elements to copy in.
 * @complexity O(hi - lo).
 * @alloc none — the destination already owns its storage.
 * @test CheatahBuiltins.SliceAssignFixedArray
 * @crtest LangFeatures.ListSliceAssignment
 * @systest StdlibE2E.Builtins
 */
template <typename T, std::size_t N, typename R>
    requires requires(const R& r) { r.begin(); r.end(); }
void slice_assign(std::array<T, N>& v, long long lo, long long hi, const R& rhs) {
    constexpr auto n = static_cast<long long>(N);
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (lo > n) lo = n;
    if (hi > n) hi = n;
    if (hi < lo) hi = lo;
    const auto want = static_cast<std::size_t>(hi - lo);
    const auto got = static_cast<std::size_t>(std::distance(rhs.begin(), rhs.end()));
    if (got != want) {
        throw std::runtime_error("array: a slice assignment fills a fixed extent — the source has " +
                                 std::to_string(got) + " element(s) for " + std::to_string(want) +
                                 " slot(s)");
    }
    std::copy(rhs.begin(), rhs.end(), v.begin() + static_cast<std::size_t>(lo));
}

/// @cond INTERNAL
/// A fixed-size array has nothing to delete — its extent is part of its type.
template <typename T, std::size_t N>
void slice_assign(std::array<T, N>& v, long long lo, long long hi, empty_seq /*empty*/) {
    (void)v; (void)lo; (void)hi;
    throw std::runtime_error(
        "array: a[lo:hi] = [] has no meaning — a fixed-size array is filled, not resized");
}
/// @endcond

/// @cond INTERNAL
/// A string is immutable, exactly as in Python, so `s[a:b] = …` has no meaning. Declared (rather
/// than left to fail on overload resolution) so the error is a sentence instead of a page of
/// candidate templates.
template <typename R>
void slice_assign(std::string& s, long long lo, long long hi, const R& rhs) {
    static_assert(sizeof(R) == 0,
                  "cheatah: a str is immutable — s[a:b] = ... is not allowed (as in Python). "
                  "Build a new string, e.g. s = s[:a] + replacement + s[b:].");
    (void)s; (void)lo; (void)hi; (void)rhs;
}
/// @endcond

/**
 * Replace `v[lo:hi]` with the elements of @p rhs, resizing the list (Python list semantics).
 *
 * Bounds follow @ref slice: negatives count from the end, out-of-range values clamp, and a
 * reversed range is an insertion point. The list GROWS or SHRINKS to fit @p rhs, so
 * `xs[1:3] = []` deletes those elements and `xs[1:1] = ys` inserts without removing any.
 *
 * @p rhs is copied into a temporary before the list is touched. That is what makes
 * `xs[1:3] = xs` and an overlapping source well defined instead of undefined: `erase` would
 * otherwise invalidate the very range `insert` is reading from.
 * @tparam T the element type.
 * @tparam R the source range type.
 * @param v the list to modify in place.
 * @param lo start index (negative counts from the end).
 * @param hi end index, or @ref slice_end for "to the end".
 * @param rhs the elements to write.
 * @complexity O(size + |rhs|) — the tail shifts when the length changes.
 * @alloc one temporary holding @p rhs, plus a reallocation when the list grows.
 * @test CheatahBuiltins.SliceAssignList
 * @test CheatahBuiltins.SliceAssignAliasing
 * @crtest LangFeatures.ListSliceAssignment
 * @systest StdlibE2E.Builtins
 */
template <typename T, typename R>
    requires requires(const R& r) { r.begin(); r.end(); }
void slice_assign(std::vector<T>& v, long long lo, long long hi, const R& rhs) {
    const auto n = static_cast<long long>(v.size());
    lo = detail::norm_index(lo, n);
    hi = (hi == slice_end) ? n : detail::norm_index(hi, n);
    if (lo < 0) lo = 0;
    if (lo > n) lo = n;
    if (hi > n) hi = n;
    if (hi < lo) hi = lo;
    // Materialise FIRST — @p rhs may be `v` itself, or a range into it.
    std::vector<T> tmp(rhs.begin(), rhs.end());
    const auto ulo = static_cast<std::size_t>(lo);
    if (static_cast<long long>(tmp.size()) == hi - lo) {
        std::copy(tmp.begin(), tmp.end(), v.begin() + ulo);  // same length: no resize
        return;
    }
    v.erase(v.begin() + ulo, v.begin() + static_cast<std::size_t>(hi));
    v.insert(v.begin() + ulo, tmp.begin(), tmp.end());
}

/// @cond INTERNAL
/// A list slice is filled from a sequence, never from a bare element — `xs[1:3] = 9` is the
/// mistake this catches, with a sentence rather than a missing-begin() error.
template <typename T, typename R>
    requires(!requires(const R& r) { r.begin(); r.end(); })
void slice_assign(std::vector<T>& v, long long lo, long long hi, const R& rhs) {
    static_assert(sizeof(R) == 0,
                  "cheatah: a list slice is assigned from a list — write xs[a:b] = [value], "
                  "not xs[a:b] = value.");
    (void)v; (void)lo; (void)hi; (void)rhs;
}
/// @endcond

} // namespace cheatah::builtins
