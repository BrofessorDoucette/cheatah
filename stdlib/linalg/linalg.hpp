// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file linalg.hpp
 * @brief cheatah `linalg` — umbrella header for the linear-algebra library
 *        (cheatah::linalg). Include this to pull in the whole public surface, or
 *        include the individual headers below for finer-grained dependencies.
 */
#include "routines.hpp"  // numpy-style routines on ndarray (matmul/solve/inv/svd/eig/…)
#include "simd.hpp"      // host SIMD feature reporting
