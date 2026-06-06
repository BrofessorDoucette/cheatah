#pragma once

namespace cheatah::linalg {

// Linear-algebra library version string (e.g. "0.0.1"), sourced from the CMake
// PROJECT_VERSION at build time.
const char* version() noexcept;

} // namespace cheatah::linalg
