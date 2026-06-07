#pragma once

/**
 * @file simd.hpp
 * @brief cheatah `linalg` — SIMD capability reporting for the linear-algebra core.
 *
 * The library exploits SIMD (and, later, GPU) for the performance-critical kernels
 * behind cheatah's optimization problems. These helpers expose what the *current
 * build* can dispatch to, so callers, tests, and benchmarks can record the
 * hardware-acceleration tier a result was produced on. The actual kernels live in
 * the routines (routines.hpp). Tested in tests/linalg/smoke_test.cpp.
 */
#include <string>

namespace cheatah::linalg {

/**
 * Instruction sets this build targets, e.g. "AVX2;FMA" (x86-64), "NEON" (ARM), or "scalar".
 * @return `;`-separated feature list.
 * @note O(1); reflects compile-time target flags (e.g. -march=native), not a runtime CPUID
 *   probe. Allocates the returned std::string.
 * @test LinalgSmoke.SimdFeaturesReported
 */
std::string simd_features();

/**
 * Width, in `double`s, of the widest SIMD lane this build targets (1 = scalar, 2 = SSE2/NEON, 4
 *   = AVX, 8 = AVX-512).
 * @return lane width.
 * @complexity O(1).
 * @alloc none. Useful for sizing blocked kernels.
 * @test LinalgSmoke.SimdFeaturesReported
 */
int simd_lane_doubles() noexcept;

namespace detail {
/**
 * Normalize a feature list: "scalar" if empty, else unchanged.
 * @param features raw feature list.
 * @return @p features, or "scalar" when empty.
 * @note O(n); factored out so the no-SIMD fallback is testable on SIMD-capable build machines.
 *   Allocates the returned std::string.
 * @test LinalgSmoke.SimdScalarFallback
 */
std::string scalar_if_empty(std::string features);
}  // namespace detail

} // namespace cheatah::linalg
