#include "version.hpp"

namespace cheatah::linalg {

const char* version() noexcept {
#ifdef CHEATAH_LINALG_VERSION
    return CHEATAH_LINALG_VERSION;  // injected from CMake PROJECT_VERSION
#else
    return "0.0.0";
#endif
}

} // namespace cheatah::linalg
