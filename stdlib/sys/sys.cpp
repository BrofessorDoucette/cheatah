// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// cheatah `sys` module implementation — see sys.hpp for the interface.

#include "sys.hpp"

namespace cheatah::sys {

// Definition of the storage declared `extern` in the header. Empty until the
// cheatah runtime forwards the program's arguments via cheatah_set_argv().
std::vector<std::string> argv;

/// @cond INTERNAL — the runtime hook's definition; programs read `sys.argv`
void set_argv(int argc, char** argv_) {
    argv.clear();
    if (argc < 0 || argv_ == nullptr) return;
    argv.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        argv.emplace_back(argv_[i] ? argv_[i] : "");
    }
}
/// @endcond

} // namespace cheatah::sys

// ---------------------------------------------------------------------------
// The stable, exported entry point the cheatah runtime resolves with dlsym and
// calls (before purr_main) to hand the program its command-line arguments. It is
// `extern "C"` so the runtime can find it by a fixed name, and given default
// visibility so it survives in the loaded module's dynamic symbol table. Only
// programs that `import sys` carry this symbol; for the rest the runtime simply
// finds nothing and forwards no arguments.
#if defined(_WIN32)
#define CHEATAH_SYS_EXPORT extern "C" __declspec(dllexport)
#else
#define CHEATAH_SYS_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// @cond INTERNAL — the exported C hook the runtime dlsym()s; never cheatah-visible
CHEATAH_SYS_EXPORT void cheatah_set_argv(int argc, char** argv) {
    cheatah::sys::set_argv(argc, argv);
}
/// @endcond
