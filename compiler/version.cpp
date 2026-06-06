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
