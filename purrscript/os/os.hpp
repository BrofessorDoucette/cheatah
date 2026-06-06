#pragma once

// cheatah os — Python-like operating-system interface. Mirrors a practical
// subset of https://docs.python.org/3/library/os.html (built on std::filesystem).
//
// One of cheatah's standard-library MODULES. `import os` includes this header
// AND links the os library (libcheatah_os). Templated entry points
// (e.g. os.path.join) live here; the rest is compiled into the library.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cheatah::os {

// StringLike<T>: a std::string can be constructed from T — exactly what
// os.path.join() does (std::string(part)). Naming it yields a clear "constraint
// StringLike not satisfied" message while still accepting everything it does today
// (const char*, char arrays, std::string, std::string_view, …).
template <typename T>
concept StringLike = requires(const T& value) { std::string(value); };

std::string getcwd();
void chdir(const std::string& path);
std::vector<std::string> listdir(const std::string& path = ".");
void mkdir(const std::string& path);
void makedirs(const std::string& path);
void rmdir(const std::string& path);
bool remove(const std::string& path);  // true if a file was removed
void rename(const std::string& src, const std::string& dst);

std::string getenv(const std::string& name, const std::string& fallback = "");
void setenv(const std::string& name, const std::string& value, bool overwrite = true);

int getpid();
unsigned cpu_count();
int system(const std::string& command);

// os.path — the path-manipulation submodule.
namespace path {

// join("a", "b", "c") -> "a/b/c" (uses the platform separator via filesystem).
template <StringLike... Parts>
std::string join(const std::string& first, const Parts&... rest) {
    std::filesystem::path p(first);
    ((p /= std::filesystem::path(std::string(rest))), ...);
    return p.string();
}

bool exists(const std::string& p);
bool isfile(const std::string& p);
bool isdir(const std::string& p);
std::string basename(const std::string& p);
std::string dirname(const std::string& p);
std::string abspath(const std::string& p);
std::string normpath(const std::string& p);
std::uintmax_t getsize(const std::string& p);

// splitext("dir/file.purr") -> {"dir/file", ".purr"}.
std::pair<std::string, std::string> splitext(const std::string& p);

} // namespace path
} // namespace cheatah::os
