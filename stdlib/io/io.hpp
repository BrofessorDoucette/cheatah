// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file io.hpp
 * @brief cheatah `io` — Python-like input/output, surfaced as free functions and a
 *        `File` object (a .purr program writes `io.print(...)`, `io.open(...)`).
 *
 * `import io` includes this header AND links the io library (libcheatah_io); a
 * program that doesn't import io neither sees nor links it. Unit tests:
 * `stdlib/tests/io_test.cpp`; the suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Templated entry points live here (they monomorphize at the call site →
 *       tight machine code); the non-template symbols are compiled into the library.
 */
#include <complex>
#include <concepts>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace cheatah::io {

/// Streamable<T>: T can be written to a `std::ostream` — the ONLY thing str(),
/// print(), format() and File::write() actually need. Naming the requirement turns
/// a deep `operator<<` instantiation error into a clear "constraint Streamable not
/// satisfied" message, without narrowing what those templates already accept.
template <typename T>
concept Streamable = requires(std::ostream& os, const T& value) {
    { os << value } -> std::convertible_to<std::ostream&>;
};

/// HasStr<T>: T exposes a `str()` method that returns a Streamable value. This is
/// the hook a struct (or a built-in object like NDArray) implements to become
/// printable — `io.print` calls it to get something it can stream.
template <typename T>
concept HasStr = requires(const T& value) {
    { value.str() } -> Streamable;
};

// Printable<T>: what `print`/`str` actually require — NOT "is it Streamable", but
// "can it be turned into something Streamable, then streamed". True when T streams
// directly, exposes a HasStr `str()`, or is a list/dict whose elements are
// themselves Printable (checked recursively, so nested containers work).
template <typename T>
struct printable_trait : std::bool_constant<Streamable<T> || HasStr<T>> {};
template <typename T, typename A>
struct printable_trait<std::vector<T, A>> : printable_trait<T> {};
template <typename K, typename V, typename H, typename E, typename A>
struct printable_trait<std::unordered_map<K, V, H, E, A>>
    : std::bool_constant<printable_trait<K>::value && printable_trait<V>::value> {};
template <typename T>
concept Printable = printable_trait<std::remove_cvref_t<T>>::value;

/**
 * Python `str()`: stringify any streamable value.
 *
 * Renders @p value via its `operator<<` into a fresh `ostringstream`, so the result
 * matches whatever that stream insertion produces (e.g. default float precision).
 * @param value the value to render.
 * @return @p value formatted as text.
 * @complexity O(n) in the output length.
 * @alloc allocates the result string (via an ostringstream).
 * @test CheatahIo.StrFormatsPythonStyle
 * @crtest IoCompileRun.Str
 * @systest StdlibE2E.Io
 */
template <Streamable T>
std::string str(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}
/**
 * `str()` for a `std::string` — identity overload.
 * @param value the string.
 * @return a copy of @p value.
 * @complexity O(n).
 * @alloc allocates the result copy.
 * @test CheatahIo.StrFormatsPythonStyle
 * @crtest IoCompileRun.Str
 * @systest StdlibE2E.Io
 */
std::string str(const std::string& value);
/**
 * `str()` for a bool — Python spelling.
 *
 * Overrides the default streaming of a bool (`1`/`0`) to emit Python's capitalized
 * `True`/`False` instead.
 * @param b the boolean.
 * @return `"True"` or `"False"`.
 * @complexity O(1).
 * @alloc allocates the small result string.
 * @test CheatahIo.StrFormatsPythonStyle
 * @crtest IoCompileRun.Str
 * @systest StdlibE2E.Io
 */
std::string str(bool b);
/**
 * `str()` for the byte-width integers `i8`/`u8` (`std::int8_t`/`std::uint8_t` == `signed char`/
 * `unsigned char`). Streaming a `char`-sized type prints a CHARACTER; these promote to a wider
 * integer so `i8`/`u8` render as NUMBERS. Declared before `print`/`repr`/`str(vector)` so those
 * templates see them (a fundamental-type argument gets no ADL). Plain `char` is a distinct type
 * and deliberately unmatched (cheatah has no bare-`char` value — single chars are 1-char strings).
 * @param v the `i8` value to render.
 * @return the value's decimal digits.
 * @complexity O(1).
 * @alloc allocates the small result string.
 * @test CheatahIo.StrByteWidthIntsAreNumbers
 */
std::string str(signed char v);
/**
 * `str()` for `u8` (`std::uint8_t`) — numeric, not a character. See @ref str(signed char).
 * @param v the `u8` value to render.
 * @return the value's decimal digits.
 * @complexity O(1).
 * @alloc allocates the small result string.
 * @test CheatahIo.StrByteWidthIntsAreNumbers
 */
std::string str(unsigned char v);

/**
 * Fixed-point float formatting — Python's `f"{x:.2f}"` / `"%.2f" % x` as a function.
 *
 * Renders @p value with exactly @p places digits after the decimal point, correctly
 * rounded (the C `printf("%.*f")` semantics, which match Python's fixed-point format
 * of the same double). Negative @p places is treated as 0; @p places is capped at 17
 * (beyond a double's meaningful precision).
 * @param value the number to format.
 * @param places digits after the decimal point (clamped to [0, 17]).
 * @return the fixed-point text (e.g. `fixed(2.675, 2)` -> `"2.67"`).
 * @complexity O(places) plus the integer-digit count.
 * @alloc allocates the result string.
 * @test CheatahIo.FixedFormatsAndRounds
 * @crtest IoCompileRun.Fixed
 * @systest StdlibE2E.Io
 */
std::string fixed(double value, long long places);

// repr() renders a value the way it appears INSIDE a container (strings quoted).
// Forward-declared here so the str(list)/str(dict) overloads below can call it for
// their elements; the definitions live further down.
template <Streamable T>
std::string repr(const T& value);
std::string repr(const std::string& value);  // NOLINT(readability-redundant-declaration): must precede the container templates that call it (ADL cannot find it for std::string)
std::string repr(const char* value);         // NOLINT(readability-redundant-declaration): same — the documented declaration below is the API-doc anchor
template <typename T>
    requires(HasStr<T> && !Streamable<T>)
std::string repr(const T& value);
template <std::floating_point T>
std::string repr(const std::complex<T>& z);
template <typename T>
    requires Printable<T>
std::string repr(const std::vector<T>& v);
template <typename K, typename V, typename H, typename E, typename A>
    requires(Printable<K> && Printable<V>)
std::string repr(const std::unordered_map<K, V, H, E, A>& m);

/**
 * `str()` for a type with a `str()` method (a cheatah struct that implements it, or
 * a built-in object like NDArray): defer to that method's rendering.
 * @param value a HasStr value.
 * @return `value.str()`, itself run through str() so the result is a string.
 * @complexity O(n) in the output length, plus the cost of `value.str()` itself.
 * @alloc allocates the result string, plus whatever `value.str()` allocates.
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename T>
    requires(HasStr<T> && !Streamable<T>)
std::string str(const T& value) {
    return str(value.str());
}
/**
 * `str()` for a complex number — Python-style `a+bj` / `a-bj` (not `std::complex`'s
 * default `(a,b)`), so a complex scalar and a complex `ndarray` element read alike.
 * Negative zero in either part is flushed to `+0` (a conjugate prints `1+0j`).
 * @param z the complex value.
 * @return the `a±bj` rendering.
 * @complexity O(1) (renders the two components).
 * @alloc allocates the result string (via an ostringstream).
 * @test CheatahIo.StrRendersComplex
 * @systest StdlibE2E.Io
 */
template <std::floating_point T>
std::string str(const std::complex<T>& z) {
    const auto nz = [](T x) -> T { return x == T{0} ? T{0} : x; };
    std::ostringstream os;
    os << nz(z.real());
    if (z.imag() < T{0}) {
        os << '-' << nz(-z.imag()) << 'j';
    } else {
        os << '+' << nz(z.imag()) << 'j';
    }
    return os.str();
}
/**
 * `str()` for a list — Python `[a, b, c]`. Elements are rendered with repr(), so a
 * `list[str]` prints with quotes (`['a', 'b']`), matching Python.
 * @param v the list (its element type must be Printable).
 * @return the bracketed rendering.
 * @complexity O(total output length).
 * @alloc allocates the result string, plus a temporary string per element (repr()).
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename T>
    requires Printable<T>
std::string str(const std::vector<T>& v) {
    std::ostringstream os;
    os << '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i != 0) os << ", ";
        os << repr(v[i]);
    }
    os << ']';
    return os.str();
}
/**
 * `str()` for a dict — Python `{k: v, …}`. Iteration order is unspecified (it is a
 * hash map). Keys and values are rendered with repr().
 * @param m the dict (key and value types must be Printable).
 * @return the brace-wrapped rendering.
 * @complexity O(total output length).
 * @alloc allocates the result string, plus a temporary string per key/value (repr()).
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename K, typename V, typename H, typename E, typename A>
    requires(Printable<K> && Printable<V>)
std::string str(const std::unordered_map<K, V, H, E, A>& m) {
    std::ostringstream os;
    os << '{';
    bool first = true;
    for (const auto& [k, val] : m) {
        if (!first) os << ", ";
        first = false;
        os << repr(k) << ": " << repr(val);
    }
    os << '}';
    return os.str();
}

/**
 * Python `print(*args)`: space-separated, newline-terminated, to stdout (sep=' ', end='\n').
 *
 * Inserts a single space between consecutive arguments (none before the first) and always
 * ends with a trailing `\n`; with no arguments it writes just that newline (blank line).
 * Output is meant to be NICE AND READABLE by default: a struct (which the compiler gives a
 * `cheatah_pretty_print` member) is rendered over multiple indented lines, e.g.
 * `Point(\n    x = 1,\n    y = 2\n)`. Use @ref rprint to print a struct in its compact form.
 * @param args zero or more printable values.
 * @complexity O(total output length).
 * @alloc allocates a temporary string per str()-routed arg; a struct's pretty-printer
 *   streams straight to stdout instead.
 * @concurrency writes to the shared `std::cout`; concurrent prints from several threads
 *   do not race but may interleave their characters.
 * @test CheatahIo.PrintWritesSpaceSeparatedLine, CheatahIo.PrintNoArgsIsJustNewline
 * @crtest IoCompileRun.Print
 * @systest StdlibE2E.Io
 */
template <Printable... Args>
void print(const Args&... args) {
    std::size_t i = 0;
    // A struct exposes a `cheatah_pretty_print` member (the compiler generates it): use it for
    // the readable multi-line layout. Everything else uses str() (the compact Python form).
    auto one = [&](const auto& v) {
        std::cout << (i++ ? " " : "");
        if constexpr (requires(std::ostream& o) { v.cheatah_pretty_print(o, 0LL); })
            v.cheatah_pretty_print(std::cout, 0LL);
        else
            std::cout << str(v);
    };
    (one(args), ...);
    std::cout << '\n';
}

/**
 * Python `print` but RAW: a struct prints in its COMPACT `Name(field=value, …)` form (exactly
 * as stored) instead of the pretty multi-line layout @ref print uses; otherwise identical
 * (space-separated, newline-terminated). Reach for it when you want a struct exactly as it is
 * rather than the default human-readable formatting.
 * @param args zero or more printable values.
 * @complexity O(total output length).
 * @alloc each arg is routed through str(), allocating temporary strings.
 * @concurrency writes to the shared `std::cout`; concurrent prints from several threads
 *   do not race but may interleave their characters.
 * @test CheatahIo.RprintIsCompact
 * @crtest IoCompileRun.Rprint
 * @systest StdlibE2E.Io
 */
template <Printable... Args>
void rprint(const Args&... args) {
    std::size_t i = 0;
    ((std::cout << (i++ ? " " : "") << str(args)), ...);
    std::cout << '\n';
}

/**
 * Python `repr()` for a generic value — same as str() for non-strings.
 *
 * Forwards directly to str(), so non-string values get no extra quoting or escaping;
 * only the string overloads below add the surrounding quotes.
 * @param value the value to render.
 * @return @p value formatted as text.
 * @complexity O(n).
 * @alloc allocates the result string.
 * @test CheatahIo.ReprQuotesStrings
 * @crtest IoCompileRun.Repr
 * @systest StdlibE2E.Io
 */
template <Streamable T>
std::string repr(const T& value) { return str(value); }
/**
 * `repr()` for a complex number — same Python-style `a±bj` as @ref str (numbers are
 * not quoted), so a `list[complex]` renders its elements readably.
 * @param z the complex value.
 * @return the `a±bj` rendering.
 * @complexity O(1).
 * @alloc allocates the result string.
 * @test CheatahIo.StrRendersComplex
 * @systest StdlibE2E.Io
 */
template <std::floating_point T>
std::string repr(const std::complex<T>& z) { return str(z); }
/**
 * `repr()` for a `std::string` — quoted (Python repr).
 *
 * Wraps the text in single quotes but does not escape embedded quotes, backslashes, or
 * control characters, so the result is not a faithful round-trip of Python's repr.
 * @param value the string.
 * @return @p value wrapped in single quotes.
 * @complexity O(n).
 * @alloc allocates the result string.
 * @test CheatahIo.ReprQuotesStrings
 * @crtest IoCompileRun.Repr
 * @systest StdlibE2E.Io
 */
std::string repr(const std::string& value);  // NOLINT(readability-redundant-declaration): the API-doc anchor; the early forward declaration above is load-bearing
/**
 * `repr()` for a C string — quoted (Python repr).
 *
 * Copies the NUL-terminated input into a `std::string` and wraps it in single quotes;
 * like the string overload it performs no escaping, and @p value must not be null.
 * @param value the C string.
 * @return @p value wrapped in single quotes.
 * @complexity O(n).
 * @alloc allocates the result string.
 * @test CheatahIo.ReprQuotesStrings
 * @crtest IoCompileRun.Repr
 * @systest StdlibE2E.Io
 */
std::string repr(const char* value);  // NOLINT(readability-redundant-declaration): same
/**
 * repr() of a `str()`-having object is its `str()` (like Python, repr defers to the
 * type's own rendering).
 * @param value a value whose type exposes `str()`.
 * @return `value.str()`.
 * @complexity O(n). @alloc allocates the result string.
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename T>
    requires(HasStr<T> && !Streamable<T>)
std::string repr(const T& value) {
    return str(value);
}
/**
 * repr() of a list equals its str() (Python: `repr([1, 2]) == '[1, 2]'`).
 * @param v the list (its element type must be Printable).
 * @return the bracketed rendering, elements repr'd.
 * @complexity O(n). @alloc allocates the result string.
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename T>
    requires Printable<T>
std::string repr(const std::vector<T>& v) {
    return str(v);
}
/**
 * repr() of a dict equals its str() (`{k: v, …}`, unspecified order).
 * @param m the dict (key and value types must be Printable).
 * @return the brace-wrapped rendering, keys/values repr'd.
 * @complexity O(n). @alloc allocates the result string.
 * @test CheatahIo.StrRendersContainersAndObjects
 * @systest StdlibE2E.Io
 */
template <typename K, typename V, typename H, typename E, typename A>
    requires(Printable<K> && Printable<V>)
std::string repr(const std::unordered_map<K, V, H, E, A>& m) {
    return str(m);
}

namespace detail {
/**
 * Base case of the recursive formatter: emit the remainder of @p fmt verbatim.
 * @param os destination stream.
 * @param fmt remaining format text (no placeholders left to fill).
 */
void format_into(std::ostringstream& os, std::string_view fmt);
/**
 * Recursive step: write text up to the next `{}`, substitute @p arg, recurse on the rest.
 * @param os destination stream.
 * @param fmt remaining format text.
 * @param arg value substituted at the next `{}` placeholder.
 * @param rest values for the remaining placeholders.
 */
template <Streamable T, typename... Rest>
void format_into(std::ostringstream& os, std::string_view fmt, const T& arg, const Rest&... rest) {
    const std::size_t brace = fmt.find("{}");
    if (brace == std::string_view::npos) {
        os << fmt;  // more args than placeholders — drop the extras
        return;
    }
    os << fmt.substr(0, brace) << arg;
    format_into(os, fmt.substr(brace + 2), rest...);
}
} // namespace detail

/**
 * Sequential `{}` substitution — the common case of Python's str.format() / f-strings.
 *
 * Replaces each `{}` left-to-right with the corresponding argument (streamed via its
 * `operator<<`); surplus arguments are silently dropped, and any `{}` left without an
 * argument is emitted literally rather than raising. Does not support indexed or named
 * fields (`{0}`, `{name}`) or escaped braces (`{{`).
 * @param fmt format string with `{}` placeholders.
 * @param args values substituted left-to-right (extras dropped, missing placeholders left
 *   as-is).
 * @return the formatted string.
 * @complexity O(len(fmt) + total arg output).
 * @alloc allocates the result string (via an ostringstream).
 * @test CheatahIo.FormatSubstitutesBraces, CheatahIo.FormatMultiArgAndExtraArgs
 * @crtest IoCompileRun.Format

 */
template <Streamable... Args>
std::string format(std::string_view fmt, const Args&... args) {
    std::ostringstream os;
    detail::format_into(os, fmt, args...);
    return os.str();
}

/**
 * Python `input(prompt="")`: write @p prompt, read one line from stdin.
 *
 * Writes (and flushes) @p prompt only when non-empty, then reads one line via getline;
 * at EOF or on a blank line it returns an empty string rather than signaling end-of-input.
 * @param prompt text shown before reading (no newline added).
 * @return the line read, with the trailing newline stripped.
 * @complexity O(line length).
 * @alloc allocates the returned string.
 * @concurrency blocks the calling thread until a full line (or EOF) arrives on stdin.
 * @test CheatahIo.InputReadsALine
 * @note No compile-run test: io.input reads stdin, which the e2e harness does not
 *       feed, so it is intentionally skipped in tests/purrc/io_cr_test.cpp.
 */
std::string input(std::string_view prompt = "");

/**
 * @brief A Python-like file object over `std::fstream`.
 *
 * RAII closes on scope exit — the C++ analog of `with open(...) as f:`. Move-only
 * (copying a file handle is meaningless), like Python file objects.
 */
class File {
public:
    /**
     * Construct a closed file (no stream attached).
     *
     * Leaves the underlying stream default-constructed and unopened, so is_open() is false
     * until a later open(); read/write calls on it are no-ops that fail silently.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahIo.FileIsOpenAndClose
     * @crtest IoCompileRun.IsOpenAndClose
     * @systest StdlibE2E.Io
     */
    File() = default;
    /**
     * Open @p path in @p mode (the open() free function's workhorse).
     *
     * Translates the Python mode and opens the stream; failure (e.g. missing file in `r`)
     * is not thrown — it leaves is_open() false, so callers should check before using it.
     * @param path filesystem path.
     * @param mode Python-style mode (`r`/`w`/`a`, optional `+`/`b`).
     * @complexity O(1) plus the OS open.
     * @alloc allocates the stream's file buffer on a successful open.
     * @test CheatahIo.FileWriteThenReadWhole
     * @crtest IoCompileRun.OpenWriteRead
     * @systest StdlibE2E.Io
     */
    File(const std::string& path, std::string_view mode);
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    /** Move-construct, taking over the other handle (the moved-from File becomes closed). */
    File(File&&) noexcept = default;
    /**
     * Move-assign, taking over the other handle (the moved-from File becomes closed).
     * @return reference to this File.
     */
    File& operator=(File&&) noexcept = default;
    /**
     * Close the stream if still open.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahIo.FileIsOpenAndClose
     * @crtest IoCompileRun.IsOpenAndClose
     * @systest StdlibE2E.Io
     */
    ~File();

    /**
     * (Re)open @p path in @p mode.
     *
     * Opens the stream on @p path; it does not first close an already-open handle, so reuse
     * this on a closed File. Mode follows Python: `r` read, `w` truncate-write, `a` append,
     * `+` adds the opposite direction, `b` binary; an unrecognized/empty mode defaults to `r`.
     * @param path filesystem path.
     * @param mode Python-style mode string.
     * @complexity O(1) plus the OS open.
     * @alloc none.
     * @test CheatahIo.FileWriteThenReadWhole
     * @crtest IoCompileRun.OpenWriteRead
     * @systest StdlibE2E.Io
     */
    void open(const std::string& path, std::string_view mode);
    /**
     * Is the underlying stream open?
     * @return true iff a file is attached and open.
     * @complexity O(1).
     * @alloc none.
     * @test CheatahIo.FileIsOpenAndClose
     * @crtest IoCompileRun.IsOpenAndClose
     * @systest StdlibE2E.Io
     */
    bool is_open() const;
    /**
     * Close the underlying stream (no-op if already closed).
     * @complexity O(1).
     * @alloc none.
     * @test CheatahIo.FileIsOpenAndClose
     * @crtest IoCompileRun.IsOpenAndClose
     * @systest StdlibE2E.Io
     */
    void close();

    /**
     * Read the whole remaining file.
     *
     * Drains the stream buffer from the current position to EOF in one shot, so a prior
     * readline()/read() returns only what is left; returns "" at EOF or on a closed file.
     * @return the remaining bytes as one string.
     * @complexity O(n) in bytes read.
     * @alloc allocates the returned string (buffered through a stringstream).
     * @test CheatahIo.FileWriteThenReadWhole
     * @crtest IoCompileRun.OpenWriteRead
     * @systest StdlibE2E.Io
     */
    std::string read();                    // whole remaining file
    /**
     * Read the next line.
     *
     * Consumes through the next `\n` (which is discarded). Because both a genuine empty line
     * and EOF yield "", the return value alone cannot distinguish them — check is_open()/EOF
     * separately if that matters.
     * @return the line with its newline stripped; `""` at EOF.
     * @complexity O(line length).
     * @alloc allocates the returned string.
     * @test CheatahIo.FileReadlineThenReadlines
     * @crtest IoCompileRun.Readline
     * @systest StdlibE2E.Io
     */
    std::string readline();                // next line, no newline; "" at EOF
    /**
     * Read all remaining lines.
     *
     * Repeatedly getlines from the current position until EOF, pushing each newline-stripped
     * line; returns an empty vector at EOF, and a trailing newline does not produce a final
     * empty element.
     * @return a vector of lines (newlines stripped).
     * @complexity O(n) in bytes.
     * @alloc allocates the vector and each line string.
     * @test CheatahIo.FileReadlineThenReadlines
     * @crtest IoCompileRun.Readlines
     * @systest StdlibE2E.Io
     */
    std::vector<std::string> readlines();   // all remaining lines

    /**
     * Write a streamable value to the file.
     *
     * Streams @p value through `operator<<` exactly as written — no separator and no trailing
     * newline are added (unlike print()), so the caller supplies any `\n`; data may stay
     * buffered until the stream is flushed or the File is closed.
     * @param value any streamable value.
     * @complexity O(output length).
     * @alloc none (writes straight to the stream buffer).
     * @warning A failed write is not reported: on a closed File or a failed stream the
     *   data is silently dropped (only the stream's error state records it) — there is
     *   no exception and no return value.
     * @test CheatahIo.FileWriteThenReadWhole, CheatahIo.FileAppendMode
     * @crtest IoCompileRun.OpenWriteRead
     * @systest StdlibE2E.Io
     */
    template <Streamable T>
    void write(const T& value) { stream_ << value; }

private:
    /// Map a Python mode string (`r`/`w`/`a`, optional `+`/`b`) to `std::ios::openmode`.
    static std::ios::openmode translate_mode(std::string_view mode);
    std::fstream stream_;
};

/**
 * Python `open(path, mode="r")` — construct and return a File.
 *
 * Constructs a File on @p path (defaulting to read mode) and returns it by move; as with the
 * File constructor a failed open does not throw, so check is_open() on the result.
 * @param path filesystem path.
 * @param mode Python-style mode string.
 * @return an open File (move-returned).
 * @complexity O(1) plus the OS open.
 * @alloc constructs a File; no heap of our own.
 * @test CheatahIo.FileWriteThenReadWhole
 * @crtest IoCompileRun.OpenWriteRead
 * @systest StdlibE2E.Io
 */
File open(const std::string& path, std::string_view mode = "r");

/**
 * Read a whole file into a string in one call (binary-safe — preserves every byte, including
 *   NULs).
 *
 * Opens @p path in binary mode and slurps the entire buffer in one read; an empty return is
 * ambiguous between a missing/unopenable file and a genuinely empty file.
 * @param path filesystem path.
 * @return the file's contents, or "" if it cannot be opened.
 * @complexity O(file size).
 * @alloc allocates the returned string.
 * @test CheatahIo.ReadFileWholeAndBinary
 * @crtest IoCompileRun.ReadFile
 * @systest StdlibE2E.Io
 */
std::string read_file(const std::string& path);

} // namespace cheatah::io
