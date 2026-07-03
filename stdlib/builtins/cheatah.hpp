#pragma once
/**
 * @file cheatah.hpp
 * @brief The cheatah prelude — the one header every transpiled program includes.
 *
 * `purrc` emits exactly one `#include "cheatah.hpp"` at the top of every generated
 * translation unit (followed by one header per `import`-ed module). This consolidates
 * the standard-library headers the *generated* code leans on and the always-available
 * built-in runtime, and defines the module export macro — so the generated preamble
 * stays two lines instead of a dozen repeated `#include`s.
 *
 * It deliberately pulls in ONLY the common base. Each `import <mod>` still maps to its
 * own `<mod>.hpp`, so modules stay as separated as before — only this shared floor
 * (built-ins + the few std headers the codegen itself emits) lives here.
 */

// The built-in runtime. builtins.hpp already pulls in the bulk of what generated code
// needs — <cmath> <concepts> <stdexcept> <string> <unordered_map> <utility> <vector>
// (plus <functional> <limits> <type_traits> …) — so they are NOT repeated here.
#include "builtins.hpp"

// (No `view<C>` type — deliberately removed. A non-owning window that borrows the caller's data is a
// use-after-free waiting to happen; cheatah is C++, so we OWN the data instead. APIs that used to hand
// back a `view<str>` (e.g. regex.find) now return an owned `str`, or byte offsets you slice yourself.)

// The two std headers the codegen uses that builtins.hpp does not already provide:
//   <array>  — fixed-size array types (`map_type` emits `std::array<T, N>`).
//   <memory> — the shared buffers some modules hand back (defensive; modules that need
//              it include it themselves, but generated code may name std::shared_ptr).
#include <array>
#include <memory>

/**
 * @def PURR_EXPORT
 * @brief Linkage for a generated module's `purr_main` entry point.
 *
 * `purr_main` is the symbol the runtime resolves after `dlopen`. This expands to
 * `extern "C"` everywhere (stable, unmangled name) and additionally
 * `__declspec(dllexport)` on Windows, where a DLL only exposes symbols it explicitly
 * marks for export.
 */
#if defined(_WIN32)
#define PURR_EXPORT extern "C" __declspec(dllexport)
#else
#define PURR_EXPORT extern "C"
#endif
