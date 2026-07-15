// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "os.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <process.h>  // _getpid
#include <windows.h>
#include <bcrypt.h>   // BCryptGenRandom
#else
#include <unistd.h>   // getpid
#include <sys/random.h>  // getentropy
#endif

// Compiled (non-template) symbols of the os module. Linked into a cheatah
// executable only when the program `import os`s.
namespace cheatah::os {

namespace fs = std::filesystem;

std::string getcwd() { return fs::current_path().string(); }
void chdir(const std::string& path) { fs::current_path(path); }

std::vector<std::string> listdir(const std::string& path) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(path)) {
        names.push_back(entry.path().filename().string());
    }
    return names;
}

void mkdir(const std::string& path) { fs::create_directory(path); }
void makedirs(const std::string& path) { fs::create_directories(path); }
void rmdir(const std::string& path) { fs::remove(path); }
bool remove(const std::string& path) { return fs::remove(path); }
void rename(const std::string& src, const std::string& dst) { fs::rename(src, dst); }

std::string getenv(const std::string& name, const std::string& fallback) {
    const char* v = std::getenv(name.c_str());
    return (v != nullptr) ? std::string(v) : fallback;
}
void setenv(const std::string& name, const std::string& value, bool overwrite) {
#if defined(_WIN32)
    if (!overwrite && std::getenv(name.c_str()) != nullptr) return;  // _putenv_s always overwrites
    ::_putenv_s(name.c_str(), value.c_str());
#else
    ::setenv(name.c_str(), value.c_str(), overwrite ? 1 : 0);
#endif
}

#if defined(_WIN32)
int getpid() { return static_cast<int>(::_getpid()); }
#else
int getpid() { return static_cast<int>(::getpid()); }
#endif
unsigned cpu_count() { return std::thread::hardware_concurrency(); }
int system(const std::string& command) { return std::system(command.c_str()); }

std::string urandom(int n) {
    if (n < 0) throw std::invalid_argument("os.urandom: negative byte count");
    std::string out(static_cast<std::size_t>(n), '\0');
    if (n == 0) return out;
#if defined(_WIN32)
    if (::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()),
                          static_cast<ULONG>(out.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("os.urandom: BCryptGenRandom failed");
#else
    // getentropy is limited to 256 bytes per call; loop for larger requests. A nonzero
    // return means the OS could not supply randomness — fail loudly rather than hand back
    // predictable bytes that a caller might turn into a key.
    std::size_t off = 0;
    while (off < out.size()) {
        const std::size_t chunk = std::min<std::size_t>(out.size() - off, 256);
        if (::getentropy(out.data() + off, chunk) != 0) throw std::runtime_error("os.urandom: getentropy failed");
        off += chunk;
    }
#endif
    return out;
}

std::string module_ext() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

namespace path {

bool exists(const std::string& p) { return fs::exists(p); }
bool isfile(const std::string& p) { return fs::is_regular_file(p); }
bool isdir(const std::string& p) { return fs::is_directory(p); }
std::string basename(const std::string& p) { return fs::path(p).filename().string(); }
std::string dirname(const std::string& p) { return fs::path(p).parent_path().string(); }
std::string abspath(const std::string& p) { return fs::absolute(p).string(); }
std::string normpath(const std::string& p) { return fs::path(p).lexically_normal().string(); }
std::uintmax_t getsize(const std::string& p) { return fs::file_size(p); }

std::pair<std::string, std::string> splitext(const std::string& p) {
    const std::string ext = fs::path(p).extension().string();
    if (ext.empty()) {
        return {p, ""};
    }
    return {p.substr(0, p.size() - ext.size()), ext};
}

} // namespace path
} // namespace cheatah::os
