// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "version.hpp"

namespace cheatah {

const char* version() noexcept {
#ifdef CHEATAH_VERSION
    return CHEATAH_VERSION;  // injected from CMake PROJECT_VERSION
#else
    return "0.0.0";
#endif
}

} // namespace cheatah
