#pragma once

// cheatah io — Python-like input/output. Mirrors
// https://docs.python.org/3/tutorial/inputoutput.html.
//
// One of cheatah's standard-library MODULES. `import io` in a .purr program
// includes this header AND links the io library (libcheatah_io); a
// program that doesn't import io neither sees nor links it. Templated entry points
// live here (they monomorphize at the call site → tight machine code); the
// non-template symbols are compiled into the library.
#include <concepts>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::io {

// Streamable<T>: T can be written to a std::ostream — the ONLY thing str(),
// print(), format() and File::write() actually need. Naming the requirement turns
// a deep `operator<<` instantiation error into a clear "constraint Streamable not
// satisfied" message, without narrowing what those templates already accept.
template <typename T>
concept Streamable = requires(std::ostream& os, const T& value) {
    { os << value } -> std::convertible_to<std::ostream&>;
};

// str(x): Python str() — stringify any streamable value. The std::string and bool
// overloads are compiled into the library (bool -> True/False).
template <Streamable T>
std::string str(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}
std::string str(const std::string& value);
std::string str(bool b);

// print(*args): space-separated, newline-terminated, to stdout — like Python's
// print() with sep=' ', end='\n'. Routed through str() so values format the
// Python way (e.g. bool -> True/False).
template <Streamable... Args>
void print(const Args&... args) {
    std::size_t i = 0;
    ((std::cout << (i++ ? " " : "") << str(args)), ...);
    std::cout << '\n';
}

// repr(x): like str(), but strings are quoted (Python repr()).
template <Streamable T>
std::string repr(const T& value) { return str(value); }
std::string repr(const std::string& value);
std::string repr(const char* value);

namespace detail {
void format_into(std::ostringstream& os, std::string_view fmt);
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

// format("{} ate {}", a, b): sequential "{}" substitution — the common case of
// Python's str.format() / f-strings.
template <Streamable... Args>
std::string format(std::string_view fmt, const Args&... args) {
    std::ostringstream os;
    detail::format_into(os, fmt, args...);
    return os.str();
}

// input(prompt=""): write the prompt, read a line from stdin, return it with no
// trailing newline (Python input()).
std::string input(std::string_view prompt = "");

// File: a Python-like file object over std::fstream. RAII closes on scope exit —
// the C++ analog of `with open(...) as f:`.
class File {
public:
    File() = default;
    File(const std::string& path, std::string_view mode);
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = default;
    File& operator=(File&&) = default;
    ~File();

    void open(const std::string& path, std::string_view mode);
    bool is_open() const;
    void close();

    std::string read();                    // whole remaining file
    std::string readline();                // next line, no newline; "" at EOF
    std::vector<std::string> readlines();   // all remaining lines

    template <Streamable T>
    void write(const T& value) { stream_ << value; }

private:
    static std::ios::openmode translate_mode(std::string_view mode);
    std::fstream stream_;
};

// open(path, mode="r"): Python open() — returns a File.
File open(const std::string& path, std::string_view mode = "r");

} // namespace cheatah::io
