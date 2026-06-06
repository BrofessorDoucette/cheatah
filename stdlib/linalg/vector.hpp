#pragma once

#include <span>

// Dense-vector primitives — the first slice of the linear-algebra surface.
//
// SCAFFOLDING STAGE: only dot() is implemented, as a correct, auto-vectorizable
// reference. Hand-written SIMD intrinsics and GPU backends will replace the hot
// loops behind this same interface (the signature is the contract; the kernel is
// an implementation detail). Built and exercised by the unit tests + benchmarks.
namespace cheatah::linalg {

// Dot product of two equal-length vectors over contiguous `double` storage.
// Throws std::invalid_argument when the spans differ in length.
double dot(std::span<const double> a, std::span<const double> b);

} // namespace cheatah::linalg
