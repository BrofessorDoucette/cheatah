#pragma once

// Umbrella header for the cheatah linear-algebra library (cheatah::linalg).
// Include this to pull in the whole public surface; or include the individual
// headers below for finer-grained dependencies.
#include "routines.hpp"  // numpy-style routines on ndarray (matmul/solve/inv/svd/eig/…)
#include "simd.hpp"      // host SIMD feature reporting
