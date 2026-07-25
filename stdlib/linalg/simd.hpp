// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file simd.hpp
 * @brief cheatah `linalg` — SIMD capability *reporting* for the linear-algebra core.
 *
 * ## How SIMD actually works in cheatah linalg (read this before changing kernels)
 *
 * There are **no hand-written SIMD intrinsics** (`__m256`, `_mm_…`) anywhere in this
 * library. SIMD comes from two portable mechanisms:
 *
 *  1. **Compiler auto-vectorization.** Every kernel in routines.{hpp,cpp} is written
 *     as a plain, contiguous, vectorization-friendly loop (e.g. matmul in `ikj`
 *     order so the inner accumulation is unit-stride). The module is compiled at
 *     `-O3 -march=native -funroll-loops` (see linalg/CMakeLists.txt; purrc compiles
 *     user programs the same way), so the compiler emits the widest vector
 *     instructions the **build target** supports — AVX-512 / AVX2+FMA / SSE / NEON.
 *  2. **Explicit `std::execution::unseq`** on the ndarray elementwise ops
 *     (ndarray.hpp) — a declarative "vectorize this" request that works for any
 *     element type.
 *
 * This file does **NOT compute anything with SIMD**. `simd_features()` and
 * `simd_lane_doubles()` only *report* what the current build targets, so tests and
 * benchmarks can record the acceleration tier a result was produced on.
 *
 * ## Behavior when NO SIMD is enabled (scalar build, or a target with no vector ISA)
 *
 * **Correctness is identical in every context — SIMD is only ever a speed
 * optimization, never a correctness dependency.** If the build targets a scalar-only
 * architecture (no `-march=native`, or hardware without AVX/SSE/NEON):
 *  - `simd_features()` returns `"scalar"` and `simd_lane_doubles()` returns `1`.
 *  - Every routine still runs and returns the **same numerical results**; the
 *    compiler simply emits scalar code, so the hot loops run lane-by-lane (slower).
 *  - `std::execution::unseq` degrades to ordinary sequential execution (it is a
 *    hint; the standard guarantees correctness regardless of backend, and it needs
 *    no TBB — unlike the `par`/`par_unseq` policies, which we do not use here).
 *
 * ## Limitations / sharp edges in all contexts
 *  - **Compile-time, not runtime, dispatch.** The ISA is fixed at build time by
 *    `-march=native`; there is no CPUID-based runtime selection. A binary built with
 *    `-march=native` on, say, an AVX-512 host may `SIGILL` on an older CPU. cheatah's
 *    model is "compile on the host that runs it" (purrc), so this is fine locally,
 *    but a binary moved to a different micro-architecture is not portable.
 *  - **Numerics may differ bit-for-bit between tiers.** FMA contraction and
 *    different reduction orders under vectorization can change the last ULP versus a
 *    scalar build; algorithms here are written to be stable, but exact equality
 *    across tiers is not guaranteed (tests compare within a tolerance).
 *  - **Scope today:** the linalg kernels are two-layer `Field` templates shipping
 *    `double` and `std::complex<double>` instantiations (NDArray/CNDArray); the
 *    templated ndarray elementwise ops vectorize for any `T`. Further element types
 *    are a matter of adding instantiations.
 *
 * The actual kernels live in routines.hpp. Tested in stdlib/tests/linalg_smoke_test.cpp.
 */
#include <string>

namespace cheatah::linalg {

/**
 * Instruction sets this build targets, e.g. "AVX2;FMA" (x86-64), "NEON" (ARM), or "scalar".
 * @return `;`-separated feature list.
 * @note O(1); reflects compile-time target flags (e.g. -march=native), not a runtime CPUID
 *   probe. Allocates the returned std::string.
 * @complexity O(1).
 * @alloc the returned feature string.
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
