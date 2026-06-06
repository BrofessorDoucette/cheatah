#include "io.hpp"

// Compiled (non-template) symbols of the io module. Linked into a cheatah
// executable only when the program `import io`s.
namespace cheatah::io {

std::string str(const std::string& value) { return value; }
std::string str(bool b) { return b ? "True" : "False"; }

std::string repr(const std::string& value) { return "'" + value + "'"; }
std::string repr(const char* value) { return "'" + std::string(value) + "'"; }

namespace detail {
void format_into(std::ostringstream& os, std::string_view fmt) {
    os << fmt;  // no args left — emit the remainder verbatim
}
} // namespace detail

std::string input(std::string_view prompt) {
    if (!prompt.empty()) {
        std::cout << prompt << std::flush;
    }
    std::string line;
    std::getline(std::cin, line);
    return line;
}

File::File(const std::string& path, std::string_view mode) { open(path, mode); }
File::~File() { close(); }

void File::open(const std::string& path, std::string_view mode) {
    stream_.open(path, translate_mode(mode));
}
bool File::is_open() const { return stream_.is_open(); }
void File::close() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

std::string File::read() {
    std::ostringstream ss;
    ss << stream_.rdbuf();
    return ss.str();
}
std::string File::readline() {
    std::string line;
    std::getline(stream_, line);
    return line;
}
std::vector<std::string> File::readlines() {
    std::vector<std::string> out;
    std::string line;
    while (std::getline(stream_, line)) {
        out.push_back(line);
    }
    return out;
}

std::ios::openmode File::translate_mode(std::string_view mode) {
    // Python modes: r / w / a (+ optional '+' for read-update, 'b' for binary).
    std::ios::openmode m{};
    const bool plus = mode.find('+') != std::string_view::npos;
    if (mode.find('b') != std::string_view::npos) m |= std::ios::binary;
    if (mode.find('r') != std::string_view::npos) { m |= std::ios::in; if (plus) m |= std::ios::out; }
    if (mode.find('w') != std::string_view::npos) { m |= std::ios::out | std::ios::trunc; if (plus) m |= std::ios::in; }
    if (mode.find('a') != std::string_view::npos) { m |= std::ios::out | std::ios::app; if (plus) m |= std::ios::in; }
    if (m == std::ios::openmode{}) m = std::ios::in;  // default 'r'
    return m;
}

File open(const std::string& path, std::string_view mode) { return File(path, mode); }

} // namespace cheatah::io
