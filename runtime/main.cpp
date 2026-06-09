// cheatah — the host executable that LOADS and RUNS compiled cheatah
// programs. It dlopens a module produced by purrc, resolves its `purr_main`
// entry point, and calls it.
//
//   cheatah <program.so>
//
// Fully headless: the program is self-contained (it statically links the stdlib
// modules it imported), so the host just validates, loads, and runs it.

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>          // LoadLibrary / GetProcAddress / FreeLibrary
#else
#include <dlfcn.h>            // dlopen / dlsym / dlclose
#include <sys/stat.h>        // POSIX permission bits (world-writable check)
#endif

#include "version.hpp"

namespace {

using PurrMain = void (*)();
// The optional argv hook exported by the `sys` module (present in programs that
// `import sys`). The runtime forwards the program's command-line arguments through
// it before running, so `sys.argv` reflects how the program was invoked.
using SetArgv = void (*)(int, char**);

// Accept the host platform's native loadable-module format by its leading magic bytes:
// ELF on Linux/BSD, Mach-O (incl. fat/universal) on macOS, PE ("MZ") on Windows.
bool has_module_magic(const std::array<unsigned char, 4>& m) {
#if defined(__APPLE__)
    const unsigned mag = unsigned(m[0]) | (unsigned(m[1]) << 8) |
                         (unsigned(m[2]) << 16) | (unsigned(m[3]) << 24);
    return mag == 0xfeedface || mag == 0xfeedfacf ||  // MH_MAGIC / MH_MAGIC_64
           mag == 0xcefaedfe || mag == 0xcffaedfe ||  // byte-swapped
           mag == 0xcafebabe || mag == 0xbebafeca;    // fat / universal
#elif defined(_WIN32)
    return m[0] == 'M' && m[1] == 'Z';                // PE: a .dll opens with the DOS stub
#else
    return m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';  // ELF
#endif
}
const char* module_format_name() {
#if defined(__APPLE__)
    return "Mach-O dynamic library";
#elif defined(_WIN32)
    return "PE dynamic-link library";
#else
    return "ELF shared object";
#endif
}

// Validate a module path before loading — which executes native code in-process. We
// can't make loading "safe" (it runs code by design), but we refuse to silently load the
// WRONG / tampered file: resolve to a canonical path (so the loader never does a search-
// path lookup of a bare name), require a regular file, reject a world-writable module
// (POSIX), and sniff the platform's binary magic. Returns the canonical path, or empty.
std::string sanitize_module_path(const std::string& raw) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(raw, ec);
    if (ec) {
        std::cerr << "cheatah: cannot resolve '" << raw << "': " << ec.message() << "\n";
        return {};
    }
    if (!std::filesystem::is_regular_file(canonical, ec) || ec) {
        std::cerr << "cheatah: refusing to load '" << canonical.string()
                  << "': not a regular file\n";
        return {};
    }
#if !defined(_WIN32)
    struct stat st {};
    if (::stat(canonical.c_str(), &st) == 0 && (st.st_mode & S_IWOTH)) {
        std::cerr << "cheatah: refusing to load world-writable module '"
                  << canonical.string() << "'\n";
        return {};
    }
#endif
    std::array<unsigned char, 4> magic{};
    std::FILE* f = std::fopen(canonical.string().c_str(), "rb");
    const bool ok = f && std::fread(magic.data(), 1, magic.size(), f) == magic.size() &&
                    has_module_magic(magic);
    if (f) std::fclose(f);
    if (!ok) {
        std::cerr << "cheatah: refusing to load '" << canonical.string()
                  << "': not a " << module_format_name() << "\n";
        return {};
    }
    return canonical.string();
}

// --- portable dynamic loading ----------------------------------------------------------
#if defined(_WIN32)
using ModuleHandle = HMODULE;
ModuleHandle module_open(const std::string& p) { return ::LoadLibraryA(p.c_str()); }
void* module_sym(ModuleHandle h, const char* n) {
    return reinterpret_cast<void*>(::GetProcAddress(h, n));
}
void module_close(ModuleHandle h) { ::FreeLibrary(h); }
std::string module_error() { return "LoadLibrary error " + std::to_string(::GetLastError()); }
#else
using ModuleHandle = void*;
ModuleHandle module_open(const std::string& p) { return ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* module_sym(ModuleHandle h, const char* n) { return ::dlsym(h, n); }
void module_close(ModuleHandle h) { ::dlclose(h); }
std::string module_error() { const char* e = ::dlerror(); return e ? e : "unknown error"; }
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2) {
        const std::string a = argv[1];
        if (a == "--version" || a == "-v") {
            std::cout << "cheatah " << cheatah::version() << "\n";
            return 0;
        }
    }
    if (argc < 2) {
        std::cerr << "usage: cheatah <program> [args...]   (a .so / .dylib / .dll built by purrc)\n"
                     "       cheatah --version\n"
                     "\n"
                     "Any [args...] after the program are forwarded to it as sys.argv\n"
                     "(sys.argv[0] is the program; sys.argv[1:] are the arguments).\n";
        return 2;
    }
    const std::string module_path = sanitize_module_path(argv[1]);
    if (module_path.empty()) {
        return 1;
    }

    ModuleHandle handle = module_open(module_path);
    if (handle == nullptr) {
        std::cerr << "cheatah: cannot load '" << module_path << "': " << module_error() << "\n";
        return 1;
    }

    auto purr_main = reinterpret_cast<PurrMain>(module_sym(handle, "purr_main"));
    if (purr_main == nullptr) {
        std::cerr << "cheatah: '" << module_path << "' has no purr_main entry point\n";
        module_close(handle);
        return 1;
    }

    // Forward the program's command-line arguments (the module path plus anything
    // after it) into the module, if it imported `sys`. argv+1 makes sys.argv[0] the
    // program and sys.argv[1:] the user arguments — Python's convention.
    if (auto set_argv = reinterpret_cast<SetArgv>(module_sym(handle, "cheatah_set_argv"))) {
        set_argv(argc - 1, argv + 1);
    }

    int rc = 0;
    try {
        purr_main();  // run the program
    } catch (const std::exception& e) {
        std::cerr << "cheatah: program error: " << e.what() << "\n";
        rc = 1;
    }

    module_close(handle);
    return rc;
}
