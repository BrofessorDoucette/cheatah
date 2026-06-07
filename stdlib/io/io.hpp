#pragma once

/**
 * @file io.hpp
 * @brief cheatah `io` — Python-like input/output, surfaced as free functions and a
 *        `File` object (a .purr program writes `io.print(...)`, `io.open(...)`).
 *        Mirrors https://docs.python.org/3/tutorial/inputoutput.html.
 *
 * `import io` includes this header AND links the io library (libcheatah_io); a
 * program that doesn't import io neither sees nor links it. Unit tests:
 * `stdlib/tests/io_test.cpp`; the suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * @note Templated entry points live here (they monomorphize at the call site →
 *       tight machine code); the non-template symbols are compiled into the library.
 */
#include <concepts>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
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

/** Python `str()`: stringify any streamable value. @param value the value to render. @return @p value formatted as text. @note O(n) in the output length; allocates the result string (via an ostringstream). @test CheatahIo.StrFormatsPythonStyle */
template <Streamable T>
std::string str(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}
/** `str()` for a `std::string` — identity overload. @param value the string. @return a copy of @p value. @note O(n); allocates the result copy. @test CheatahIo.StrFormatsPythonStyle */
std::string str(const std::string& value);
/** `str()` for a bool — Python spelling. @param b the boolean. @return `"True"` or `"False"`. @note O(1); allocates the small result string. @test CheatahIo.StrFormatsPythonStyle */
std::string str(bool b);

/** Python `print(*args)`: space-separated, newline-terminated, to stdout (sep=' ', end='\n'). @param args zero or more streamable values. @note O(total output length); each arg is routed through str() so values format the Python way (e.g. bool → True/False), allocating temporary strings. @test CheatahIo.PrintWritesSpaceSeparatedLine, CheatahIo.PrintNoArgsIsJustNewline */
template <Streamable... Args>
void print(const Args&... args) {
    std::size_t i = 0;
    ((std::cout << (i++ ? " " : "") << str(args)), ...);
    std::cout << '\n';
}

/** Python `repr()` for a generic value — same as str() for non-strings. @param value the value to render. @return @p value formatted as text. @note O(n); allocates the result string. @test CheatahIo.ReprQuotesStrings */
template <Streamable T>
std::string repr(const T& value) { return str(value); }
/** `repr()` for a `std::string` — quoted (Python repr). @param value the string. @return @p value wrapped in single quotes. @note O(n); allocates the result string. @test CheatahIo.ReprQuotesStrings */
std::string repr(const std::string& value);
/** `repr()` for a C string — quoted (Python repr). @param value the C string. @return @p value wrapped in single quotes. @note O(n); allocates the result string. @test CheatahIo.ReprQuotesStrings */
std::string repr(const char* value);

namespace detail {
/// Base case of the recursive formatter: emit the remainder of @p fmt verbatim.
void format_into(std::ostringstream& os, std::string_view fmt);
/// Recursive step: write text up to the next `{}`, substitute @p arg, recurse on the rest.
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

/** Sequential `{}` substitution — the common case of Python's str.format() / f-strings. @param fmt format string with `{}` placeholders. @param args values substituted left-to-right (extras dropped, missing placeholders left as-is). @return the formatted string. @note O(len(fmt) + total arg output); allocates the result string (via an ostringstream). @test CheatahIo.FormatSubstitutesBraces, CheatahIo.FormatMultiArgAndExtraArgs */
template <Streamable... Args>
std::string format(std::string_view fmt, const Args&... args) {
    std::ostringstream os;
    detail::format_into(os, fmt, args...);
    return os.str();
}

/** Python `input(prompt="")`: write @p prompt, read one line from stdin. @param prompt text shown before reading (no newline added). @return the line read, with the trailing newline stripped. @note O(line length); allocates the returned string. @test CheatahIo.InputReadsALine */
std::string input(std::string_view prompt = "");

/**
 * @brief A Python-like file object over `std::fstream`.
 *
 * RAII closes on scope exit — the C++ analog of `with open(...) as f:`. Move-only
 * (copying a file handle is meaningless), like Python file objects.
 */
class File {
public:
    /** Construct a closed file (no stream attached). @note O(1); no heap. @test CheatahIo.FileIsOpenAndClose */
    File() = default;
    /** Open @p path in @p mode (the open() free function's workhorse). @param path filesystem path. @param mode Python-style mode (`r`/`w`/`a`, optional `+`/`b`). @note O(1) plus the OS open; no heap of our own. @test CheatahIo.FileWriteThenReadWhole */
    File(const std::string& path, std::string_view mode);
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = default;
    File& operator=(File&&) = default;
    /** Close the stream if still open. @note O(1); no heap. @test CheatahIo.FileIsOpenAndClose */
    ~File();

    /** (Re)open @p path in @p mode. @param path filesystem path. @param mode Python-style mode string. @note O(1) plus the OS open; no heap. @test CheatahIo.FileWriteThenReadWhole */
    void open(const std::string& path, std::string_view mode);
    /** Is the underlying stream open? @return true iff a file is attached and open. @note O(1); no heap. @test CheatahIo.FileIsOpenAndClose */
    bool is_open() const;
    /** Close the underlying stream (no-op if already closed). @note O(1); no heap. @test CheatahIo.FileIsOpenAndClose */
    void close();

    /** Read the whole remaining file. @return the remaining bytes as one string. @note O(n) in bytes read; allocates the returned string (buffered through a stringstream). @test CheatahIo.FileWriteThenReadWhole */
    std::string read();                    // whole remaining file
    /** Read the next line. @return the line with its newline stripped; `""` at EOF. @note O(line length); allocates the returned string. @test CheatahIo.FileReadlineThenReadlines */
    std::string readline();                // next line, no newline; "" at EOF
    /** Read all remaining lines. @return a vector of lines (newlines stripped). @note O(n) in bytes; allocates the vector and each line string. @test CheatahIo.FileReadlineThenReadlines */
    std::vector<std::string> readlines();   // all remaining lines

    /** Write a streamable value to the file. @param value any streamable value. @note O(output length); writes straight to the stream buffer, no heap of our own. @test CheatahIo.FileWriteThenReadWhole, CheatahIo.FileAppendMode */
    template <Streamable T>
    void write(const T& value) { stream_ << value; }

private:
    /// Map a Python mode string (`r`/`w`/`a`, optional `+`/`b`) to `std::ios::openmode`.
    static std::ios::openmode translate_mode(std::string_view mode);
    std::fstream stream_;
};

/** Python `open(path, mode="r")` — construct and return a File. @param path filesystem path. @param mode Python-style mode string. @return an open File (move-returned). @note O(1) plus the OS open; constructs a File, no heap of our own. @test CheatahIo.FileWriteThenReadWhole */
File open(const std::string& path, std::string_view mode = "r");

/** Read a whole file into a string in one call (binary-safe — preserves every byte,
 *  including NULs). @param path filesystem path. @return the file's contents, or "" if
 *  it cannot be opened. @note O(file size); allocates the returned string. @test CheatahIo.ReadFileWholeAndBinary */
std::string read_file(const std::string& path);

} // namespace cheatah::io
