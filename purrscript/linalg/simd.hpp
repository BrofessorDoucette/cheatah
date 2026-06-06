#pragma once

#include <string>

// SIMD capability reporting for the linear-algebra core.
//
// The library is built to exploit SIMD (and, later, GPU) for the
// performance-critical kernels behind cheatah's optimization problems. These
// helpers expose what the *current build* can dispatch to, so callers, tests,
// and benchmarks can record the hardware-acceleration tier a result was
// produced on. The actual kernels live in the typed primitives (vector.hpp, …).
namespace cheatah::purrscript::linalg {

// Human-readable list of SIMD instruction sets this build targets, e.g.
// "AVX2;FMA" on a modern x86-64 build, "NEON" on ARM, or "scalar" when no
// vector ISA was enabled at compile time. Reflects compile-time target flags
// (e.g. -march=native), not a runtime CPUID probe.
std::string simd_features();

// Width, in `double`s, of the widest SIMD lane this build targets (1 = scalar,
// 2 = SSE2/NEON, 4 = AVX, 8 = AVX-512). Useful for sizing blocked kernels.
int simd_lane_doubles() noexcept;

} // namespace cheatah::purrscript::linalg
