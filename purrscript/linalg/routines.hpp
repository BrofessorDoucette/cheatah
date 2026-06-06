#pragma once

// cheatah linalg routines — the OUTLINE (declarations only, not yet implemented)
// of numpy's linear-algebra API, to be built on cheatah's SIMD linalg core +
// ndarray. Mirrors https://numpy.org/doc/stable/reference/routines.linalg.html.
//
// They operate on ndarray::NDArray (2-D = matrix, 1-D = vector). Implemented in
// routines.cpp with SIMD-friendly contiguous loops (the module compiles at
// -O3 -march=native).
#include <vector>

#include "ndarray.hpp"

namespace cheatah::linalg {

using ndarray::NDArray;

// ---- Matrix and vector products ----
double dot(const NDArray& a, const NDArray& b);          // 1-D dot / 2-D matmul
double vdot(const NDArray& a, const NDArray& b);
double inner(const NDArray& a, const NDArray& b);
NDArray outer(const NDArray& a, const NDArray& b);
NDArray matmul(const NDArray& a, const NDArray& b);
NDArray matrix_power(const NDArray& a, long long n);
NDArray kron(const NDArray& a, const NDArray& b);

// ---- Decompositions ----
NDArray cholesky(const NDArray& a);                       // lower-triangular L (A = L Lᵀ)
struct QR { NDArray q; NDArray r; };
QR qr(const NDArray& a);
struct SVD { NDArray u; NDArray s; NDArray vh; };
SVD svd(const NDArray& a);

// ---- Matrix eigenvalues ----
struct Eig { NDArray values; NDArray vectors; };
Eig eig(const NDArray& a);                                // general square matrix
NDArray eigvals(const NDArray& a);
Eig eigh(const NDArray& a);                               // symmetric / Hermitian
NDArray eigvalsh(const NDArray& a);

// ---- Norms and other numbers ----
double norm(const NDArray& a);                            // default: Frobenius / L2
double cond(const NDArray& a);
double det(const NDArray& a);
long long matrix_rank(const NDArray& a);
struct SLogDet { double sign; double logabsdet; };
SLogDet slogdet(const NDArray& a);
double trace(const NDArray& a);

// ---- Solving equations and inverting matrices ----
NDArray solve(const NDArray& a, const NDArray& b);        // A x = b
NDArray lstsq(const NDArray& a, const NDArray& b);        // least-squares solution
NDArray inv(const NDArray& a);
NDArray pinv(const NDArray& a);                           // Moore–Penrose pseudo-inverse

} // namespace cheatah::linalg
