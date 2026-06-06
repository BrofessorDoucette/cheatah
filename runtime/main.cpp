// cheatah-runtime — the host executable that LOADS and RUNS compiled cheatah
// programs. It dlopens a module produced by purrc, resolves its `purr_main`
// entry point, and calls it with a live Runtime the program drives.
//
//   cheatah-runtime <program.so>
//
// Fully headless. The Runtime is a minimal placeholder for future host services
// (logging, lifecycle, …) that a program may drive from inside purr_main.

#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include <sys/stat.h>

#include "runtime.hpp"

namespace {

using PurrMain = void (*)(cheatah::runtime::Runtime&);

// Validate a module path before we hand it to dlopen() — which executes native
// code in-process. We can't make loading "safe" (it runs code by design), but we
// refuse to silently load the WRONG / tampered file: resolve to a canonical path
// (so dlopen never does a library-search-path lookup of a bare name), require a
// regular file, reject world-writable modules, and sniff the ELF magic.
// Returns the canonical path, or empty string on rejection (message already printed).
std::string sanitize_module_path(const std::string& raw) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(raw, ec);
    if (ec) {
        std::cerr << "cheatah-runtime: cannot resolve '" << raw << "': " << ec.message() << "\n";
        return {};
    }
    struct stat st {};
    if (::stat(canonical.c_str(), &st) != 0) {
        std::cerr << "cheatah-runtime: cannot stat '" << canonical.string() << "'\n";
        return {};
    }
    if (!S_ISREG(st.st_mode)) {
        std::cerr << "cheatah-runtime: refusing to load '" << canonical.string()
                  << "': not a regular file\n";
        return {};
    }
    if (st.st_mode & S_IWOTH) {
        std::cerr << "cheatah-runtime: refusing to load world-writable module '"
                  << canonical.string() << "'\n";
        return {};
    }
    std::array<unsigned char, 4> magic{};
    std::FILE* f = std::fopen(canonical.c_str(), "rb");
    const bool elf = f && std::fread(magic.data(), 1, magic.size(), f) == magic.size() &&
                     magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
    if (f) std::fclose(f);
    if (!elf) {
        std::cerr << "cheatah-runtime: refusing to load '" << canonical.string()
                  << "': not an ELF shared object\n";
        return {};
    }
    return canonical.string();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cheatah-runtime <program.so>\n";
        return 2;
    }
    const std::string module_path = sanitize_module_path(argv[1]);
    if (module_path.empty()) {
        return 1;
    }

    void* handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::cerr << "cheatah-runtime: cannot load '" << module_path << "': " << dlerror() << "\n";
        return 1;
    }

    dlerror();  // clear any stale error
    auto purr_main = reinterpret_cast<PurrMain>(dlsym(handle, "purr_main"));
    if (const char* err = dlerror()) {
        std::cerr << "cheatah-runtime: '" << module_path << "' has no purr_main: " << err << "\n";
        dlclose(handle);
        return 1;
    }

    int rc = 0;
    try {
        cheatah::runtime::Runtime rt;
        purr_main(rt);  // the program runs and instructs the runtime
    } catch (const std::exception& e) {
        std::cerr << "cheatah-runtime: program error: " << e.what() << "\n";
        rc = 1;
    }

    dlclose(handle);
    return rc;
}
