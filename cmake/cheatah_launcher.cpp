// cheatah program launcher.
//
// A tiny native executable that runs a cheatah program (a purrc-built module) by
// INVOKING THE CHEATAH RUNTIME on it — so the program is still loaded and executed
// only by `cheatah`, never standalone, while the user types `myprog <args>` instead
// of `cheatah myprog.so <args>`. `biome` is built this way; so is any program built
// with cheatah_add_program().
//
// It is NOT purrc output: it carries no program logic, it only locates the runtime
// and the module and re-execs `cheatah <module> <args...>`. Paths are resolved next
// to the launcher first (so a release tree is relocatable), falling back to the
// absolute paths baked in at build time.
//
// Configured by the build with:
//   CHEATAH_PROGRAM_NAME   the program/module basename (e.g. "biome")
//   CHEATAH_MODULE_EXT     the module extension (".so" / ".dylib" / ".dll")
//   CHEATAH_RUNTIME_FALLBACK   absolute path to the `cheatah` runtime
//   CHEATAH_MODULE_FALLBACK    absolute path to the program's module

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>   // _spawnv / _P_WAIT
#include <windows.h>   // GetModuleFileNameA
#elif defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#include <unistd.h>       // execv
#else
#include <unistd.h>       // execv, readlink
#endif

#ifndef CHEATAH_PROGRAM_NAME
#define CHEATAH_PROGRAM_NAME "program"
#endif
#ifndef CHEATAH_MODULE_EXT
#define CHEATAH_MODULE_EXT ".so"
#endif
#ifndef CHEATAH_RUNTIME_FALLBACK
#define CHEATAH_RUNTIME_FALLBACK "cheatah"
#endif
#ifndef CHEATAH_MODULE_FALLBACK
#define CHEATAH_MODULE_FALLBACK CHEATAH_PROGRAM_NAME CHEATAH_MODULE_EXT
#endif

#if defined(_WIN32)
#define CHEATAH_EXE_EXT ".exe"
#else
#define CHEATAH_EXE_EXT ""
#endif

namespace {

// The directory containing this launcher executable, or empty on failure.
std::filesystem::path self_dir(const char* argv0) {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return fs::path(std::string(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        const fs::path canon = fs::canonical(buf, ec);
        if (!ec) return canon.parent_path();
    }
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return exe.parent_path();
#endif
    // Fallback: derive from argv[0] if it carries a path.
    if (argv0 != nullptr) {
        const fs::path p(argv0);
        if (p.has_parent_path()) {
            std::error_code ec;
            const fs::path abs = fs::absolute(p, ec);
            if (!ec) return abs.parent_path();
        }
    }
    return {};
}

// Prefer the sibling next to the launcher (relocatable release tree); otherwise the
// absolute path baked in at build time.
std::string resolve(const std::filesystem::path& dir, const std::string& sibling,
                    const char* fallback) {
    if (!dir.empty()) {
        std::error_code ec;
        const std::filesystem::path candidate = dir / sibling;
        if (std::filesystem::exists(candidate, ec) && !ec) return candidate.string();
    }
    return fallback;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = self_dir(argc > 0 ? argv[0] : nullptr);
    const std::string runtime =
        resolve(dir, "cheatah" CHEATAH_EXE_EXT, CHEATAH_RUNTIME_FALLBACK);
    const std::string module =
        resolve(dir, CHEATAH_PROGRAM_NAME CHEATAH_MODULE_EXT, CHEATAH_MODULE_FALLBACK);

    // Build: cheatah <module> <user args...>
    std::vector<std::string> parts = {runtime, module};
    for (int i = 1; i < argc; ++i) parts.emplace_back(argv[i]);

    std::vector<char*> spawn_argv;
    spawn_argv.reserve(parts.size() + 1);
    for (std::string& s : parts) spawn_argv.push_back(s.data());
    spawn_argv.push_back(nullptr);

#if defined(_WIN32)
    const intptr_t rc = _spawnv(_P_WAIT, runtime.c_str(), spawn_argv.data());
    return rc < 0 ? 127 : static_cast<int>(rc);
#else
    execv(runtime.c_str(), spawn_argv.data());
    // execv only returns on failure.
    std::perror("biome: cannot exec the cheatah runtime");
    return 127;
#endif
}
