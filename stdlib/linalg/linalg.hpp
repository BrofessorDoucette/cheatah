// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file linalg.hpp
 * @brief cheatah `linalg` — umbrella header for the linear-algebra library
 *        (cheatah::linalg). Include this to pull in the whole public surface, or
 *        include the individual headers below for finer-grained dependencies.
 *
 * The fixed-extent, allocation-free small vectors and matrices (`vec3f`/`mat4f`, @ref
 * cheatah::fixarray::Fixed) that used to live here now form their own header-only module,
 * @ref cheatah::fixarray — `import fixarray` for them. This module keeps the heavy,
 * shape-generic numerics on @ref cheatah::ndarray::NDArray.
 */
#include "routines.hpp"  // numpy-style routines on ndarray (matmul/solve/inv/svd/eig/…)
#include "simd.hpp"      // host SIMD feature reporting
