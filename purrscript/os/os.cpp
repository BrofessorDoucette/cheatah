#include "os.hpp"

#include <cstdlib>
#include <thread>

#include <unistd.h>  // getpid

// Compiled (non-template) symbols of the os module. Linked into a purrscript
// executable only when the program `import os`s.
namespace cheatah::purrscript::os {

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
    ::setenv(name.c_str(), value.c_str(), overwrite ? 1 : 0);
}

int getpid() { return static_cast<int>(::getpid()); }
unsigned cpu_count() { return std::thread::hardware_concurrency(); }
int system(const std::string& command) { return std::system(command.c_str()); }

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
} // namespace cheatah::purrscript::os
