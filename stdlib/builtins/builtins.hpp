#pragma once

// cheatah builtins — Python's built-in functions that are ALWAYS available
// (no import), mirroring https://docs.python.org/3/library/functions.html.
//
// This is the non-math subset (length, character/representation conversions,
// hashing). The mathematical built-ins (abs, min, max, round, pow, …) live in the
// `math` module per the project's structure. The compiler auto-includes this
// header and resolves bare calls like len("x") to builtins::len.
#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace cheatah::builtins {

// Sized<C>: C reports a .size() — strings and STL containers (list/dict/array).
template <typename C>
concept Sized = requires(const C& c) {
    { c.size() } -> std::convertible_to<std::size_t>;
};

// len(c): length of a string, or the element count of any sized container.
template <Sized C>
std::size_t len(const C& c) { return c.size(); }
std::size_t len(std::string_view s);  // C-strings / string literals

// ord(s) / chr(i): character <-> code point (byte-oriented for now).
int ord(std::string_view s);
std::string chr(int codepoint);

// hex/oct/bin(i): Python-style base reprs ("0x.."/"0o.."/"0b..", with sign).
std::string hex(long long value);
std::string oct(long long value);
std::string bin(long long value);

// ascii(s): printable-ASCII repr with non-printables escaped (\xNN), quoted.
std::string ascii(std::string_view s);

// bool/int/float(x): conversions (the compiler maps the keyword names to these).
bool to_bool(std::string_view s);
template <typename T>
    requires std::is_arithmetic_v<T>
bool to_bool(T x) { return x != T{}; }
long long to_int(std::string_view s);
long long to_int(double x);
double to_float(std::string_view s);
double to_float(long long x);

// hash(x): a hash value (content hash for strings).
std::size_t hash(std::string_view s);
template <typename T>
std::size_t hash(const T& x) { return std::hash<T>{}(x); }

} // namespace cheatah::builtins
