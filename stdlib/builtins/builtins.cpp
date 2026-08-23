// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "builtins.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cheatah::builtins {

std::size_t len(std::string_view s) { return s.size(); }

int ord(std::string_view s) { return s.empty() ? 0 : static_cast<unsigned char>(s[0]); }
std::string chr(int codepoint) {
    std::string out(1, static_cast<char>(codepoint));  // (1, c) must not become a braced list: {1, c} is two chars
    return out;
}

namespace {
std::string with_base(long long value, unsigned base, const char* prefix) {
    const bool neg = value < 0;
    auto u = neg ? static_cast<unsigned long long>(-(value + 1)) + 1ULL
                 : static_cast<unsigned long long>(value);
    std::string digits;
    const char* d = "0123456789abcdef";
    if (u == 0) {
        digits = "0";
    }
    while (u != 0) {
        digits.push_back(d[u % base]);
        u /= base;
    }
    std::reverse(digits.begin(), digits.end());
    return (neg ? "-" : "") + std::string(prefix) + digits;
}
} // namespace

std::string hex(long long value) { return with_base(value, 16, "0x"); }
std::string oct(long long value) { return with_base(value, 8, "0o"); }
std::string bin(long long value) { return with_base(value, 2, "0b"); }

std::string ascii(std::string_view s) {
    std::string r = "'";
    for (unsigned char c : s) {
        if (c == '\\') {
            r += "\\\\";
        } else if (c == '\'') {
            r += "\\'";
        } else if (c >= 32 && c < 127) {
            r += static_cast<char>(c);
        } else {
            std::ostringstream o;
            o << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            r += o.str();
        }
    }
    r += "'";
    return r;
}

bool to_bool(std::string_view s) { return !s.empty(); }
long long to_int(std::string_view s) { return std::stoll(std::string(s)); }
long long to_int(double x) { return static_cast<long long>(x); }
double to_float(std::string_view s) { return std::stod(std::string(s)); }

std::size_t hash(std::string_view s) { return std::hash<std::string_view>{}(s); }

} // namespace cheatah::builtins
