#pragma once

namespace cheatah {

// The cheatah version string (e.g. "0.1.0"), sourced from the CMake
// PROJECT_VERSION at build time. Reported by `purrc --version`.
const char* version() noexcept;

} // namespace cheatah
