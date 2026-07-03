// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>          // LoadLibrary / GetProcAddress / FreeLibrary
#else
#include <dlfcn.h>            // dlopen / dlsym / dlclose
#include <sys/stat.h>        // POSIX permission bits (world-writable check)
#endif

#include "integrity.hpp"
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

// --- integrity configuration (env + flags) ---------------------------------------------
std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Where the trusted Ed25519 public keys live by default. @p var is the override env var
// and @p leaf the default file under the cheatah config dir — used for BOTH the
// code-signing trust ($CHEATAH_TRUST / trusted.pub) and the SEPARATE runtime trust
// ($CHEATAH_RT_TRUST / trusted-runtime.pub).
std::string default_trust_path(const char* var, const char* leaf) {
    const std::string fromenv = env_or_empty(var);
    if (!fromenv.empty()) return fromenv;
    const std::string xdg = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg + "/cheatah/" + leaf;
    const std::string home = env_or_empty("HOME");
    if (!home.empty()) return home + "/.config/cheatah/" + leaf;
    return {};
}

bool truthy(const std::string& s) {
    return s == "1" || s == "strict" || s == "on" || s == "yes" || s == "true";
}

void print_usage(std::ostream& os) {
    os << "usage: cheatah [--verify[=strict]] [--trust <keyfile>] [--trust-runtime <keyfile>]\n"
          "               <program> [args...]\n"
          "       cheatah --version | --help\n"
          "\n"
          "A <program> is a .so / .dylib / .dll built by purrc. Any [args...] after it are\n"
          "forwarded as sys.argv (sys.argv[0] is the program).\n"
          "\n"
          "Integrity: a <program>.sha512 sidecar is auto-verified (corruption), and a\n"
          "<program>.rt build-runtime manifest is checked against this host (refused if the\n"
          "module needs a newer C runtime). With --verify (or CHEATAH_VERIFY=strict) a valid\n"
          "<program>.sig from a key in the trust file (--trust / CHEATAH_TRUST) is REQUIRED;\n"
          "if a SEPARATE runtime trust (--trust-runtime / CHEATAH_RT_TRUST) is set, a valid\n"
          "<program>.rt.sig is required too. Otherwise the program is refused.\n";
}

} // namespace

int main(int argc, char** argv) {
    using cheatah::integrity::Policy;

    // Integrity policy defaults from the environment; leading flags override it.
    Policy policy = truthy(env_or_empty("CHEATAH_VERIFY")) ? Policy::Strict : Policy::Off;
    std::string trust_path = default_trust_path("CHEATAH_TRUST", "trusted.pub");
    std::string rt_trust_path = default_trust_path("CHEATAH_RT_TRUST", "trusted-runtime.pub");

    // Parse leading options (before the program), so [args...] after the program still
    // forward verbatim to sys.argv.
    int i = 1;
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::cout << "cheatah " << cheatah::version() << "\n";
            return 0;
        }
        if (a == "--help" || a == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (a == "--verify" || a == "--verify=strict") {
            policy = Policy::Strict;  // policy only ESCALATES — there is no flag to turn
                                      // verification OFF, so a strict deployment (env or
                                      // wrapper) cannot be silently downgraded by argv.
        } else if (a.rfind("--trust=", 0) == 0) {
            trust_path = a.substr(8);
        } else if (a == "--trust") {
            if (i + 1 >= argc) { std::cerr << "cheatah: --trust needs a file path\n"; return 2; }
            trust_path = argv[++i];
        } else if (a.rfind("--trust-runtime=", 0) == 0) {
            rt_trust_path = a.substr(16);
        } else if (a == "--trust-runtime") {
            if (i + 1 >= argc) { std::cerr << "cheatah: --trust-runtime needs a file path\n"; return 2; }
            rt_trust_path = argv[++i];
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "cheatah: unknown option '" << a << "'\n";
            return 2;
        } else {
            break;  // the program path
        }
    }

    if (i >= argc) {
        print_usage(std::cerr);
        return 2;
    }
    const int module_index = i;
    const std::string module_path = sanitize_module_path(argv[module_index]);
    if (module_path.empty()) {
        return 1;
    }

    // Verify the module's integrity BEFORE loading it (loading runs native code). The
    // returned load_path refers to the exact bytes that were verified, so the dlopen
    // below cannot be raced onto a substituted file.
    const std::vector<std::string> trusted =
        trust_path.empty() ? std::vector<std::string>{}
                           : cheatah::integrity::load_trusted_keys(trust_path);
    const std::vector<std::string> trusted_runtime =
        rt_trust_path.empty() ? std::vector<std::string>{}
                              : cheatah::integrity::load_trusted_keys(rt_trust_path);
    cheatah::integrity::Result iv =
        cheatah::integrity::verify_module(module_path, policy, trusted, trusted_runtime);
    if (!iv.ok) {
        std::cerr << "cheatah: refusing to load '" << module_path << "': " << iv.error << "\n";
        cheatah::integrity::release(iv);
        return 1;
    }

    ModuleHandle handle = module_open(iv.load_path);
    cheatah::integrity::release(iv);  // safe to close the fd once the image is mapped
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

    // Forward the program's command-line arguments (the module path plus anything after
    // it) into the module, if it imported `sys`. Starting at module_index makes
    // sys.argv[0] the program and sys.argv[1:] the user arguments — Python's convention.
    if (auto set_argv = reinterpret_cast<SetArgv>(module_sym(handle, "cheatah_set_argv"))) {
        set_argv(argc - module_index, argv + module_index);
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
