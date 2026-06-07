#pragma once

/**
 * @file routines.hpp
 * @brief cheatah `linalg` — numpy's linear-algebra API on ndarray, with
 *        SIMD-friendly contiguous kernels (a .purr program writes `linalg.solve(A, b)`).
 *
 * `import linalg` to use it (auto-links ndarray). Unit tests:
 * stdlib/tests/linalg_routines_test.cpp; SIMD reporting tested in
 * tests/linalg/smoke_test.cpp. The suite runs under AddressSanitizer (the `asan`
 * preset) and Valgrind (security/run-valgrind.sh) on every QA-gate run.
 *
 * Routines operate on `ndarray::NDArray` (2-D = matrix, 1-D = vector). They mirror
 * https://numpy.org/doc/stable/reference/routines.linalg.html and are implemented
 * in routines.cpp (LU w/ partial pivoting, Cholesky, Householder QR, one-sided
 * Jacobi SVD, cyclic Jacobi symmetric eigen, Hessenberg + shifted-QR general
 * eigen) at -O3 -march=native so the hot loops auto-vectorize.
 *
 * @note `n` below is the matrix dimension. NDArray is double-only, so `eig`/
 *       `eigvals` return REAL eigenvalues and throw on a complex pair. Routines
 *       that extract a working copy "allocate scratch O(n²) for the factorization"
 *       even when they return a scalar.
 */
#include <vector>

#include "ndarray.hpp"

namespace cheatah::linalg {

// The routines operate on cheatah::ndarray::NDArray, re-exported unqualified for brevity in the
// signatures below. The directive below hides this re-export from the API doc generator so it
// does not emit a phantom duplicate cheatah::linalg::NDArray class in the namespace/XML structure.
/// \cond INTERNAL
using ndarray::NDArray;
/// \endcond

// ---- Matrix and vector products ----
/**
 * Dot product: 1-D inner product (vectors flattened).
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @note O(n); copies both operands into scratch O(n), returns a double.
 * @test LinalgRoutines.ProductsAndTrace
 */
double dot(const NDArray& a, const NDArray& b);          // 1-D dot / 2-D matmul
/**
 * Vector dot product (alias of @ref dot; flattens N×1/1×N).
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @note O(n); scratch O(n), returns a double.
 * @test LinalgRoutines.VdotInnerOuterKron
 */
double vdot(const NDArray& a, const NDArray& b);
/**
 * Inner product of two vectors (alias of @ref dot).
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @note O(n); scratch O(n), returns a double.
 * @test LinalgRoutines.VdotInnerOuterKron
 */
double inner(const NDArray& a, const NDArray& b);
/**
 * Outer product of two vectors.
 * @param a length-n vector.
 * @param b length-m vector.
 * @return n×m matrix aᵢbⱼ.
 * @note O(n·m); allocates a new NDArray result.
 * @test LinalgRoutines.VdotInnerOuterKron
 */
NDArray outer(const NDArray& a, const NDArray& b);
/**
 * Matrix multiply.
 * @param a m×k matrix.
 * @param b k×p matrix.
 * @return m×p product.
 * @note O(n³); allocates a new NDArray result (plus scratch copies of @p a and @p b).
 * @test LinalgRoutines.ProductsAndTrace
 */
NDArray matmul(const NDArray& a, const NDArray& b);
/**
 * Integer matrix power Aⁿ (negative n via @ref inv).
 * @param a square matrix.
 * @param n exponent.
 * @return Aⁿ.
 * @note O(n³·log|n|) by binary exponentiation; allocates a new NDArray result.
 * @test LinalgRoutines.MatrixPower
 */
NDArray matrix_power(const NDArray& a, long long n);
/**
 * Kronecker product.
 * @param a m×k matrix.
 * @param b p×q matrix.
 * @return (m·p)×(k·q) block product.
 * @note O(n⁴) in the output area; allocates a new NDArray result.
 * @test LinalgRoutines.VdotInnerOuterKron
 */
NDArray kron(const NDArray& a, const NDArray& b);

// ---- Decompositions ----
/**
 * Cholesky factor of a symmetric positive-definite matrix (throws otherwise).
 * @param a square SPD matrix.
 * @return lower-triangular L with A = L·Lᵀ.
 * @note O(n³); allocates a new NDArray result.
 * @test LinalgRoutines.CholeskyAndQR
 */
NDArray cholesky(const NDArray& a);                       // lower-triangular L (A = L Lᵀ)
/** Result of qr(): A = q·r with orthonormal q and upper-triangular r. */
struct QR {
    NDArray q;  ///< Orthonormal columns, m×n (the Q in A = Q·R).
    NDArray r;  ///< Upper-triangular factor, n×n (the R in A = Q·R).
};
/**
 * Reduced QR via Householder reflections (requires rows ≥ cols).
 * @param a m×n matrix.
 * @return @ref QR with q (m×n, orthonormal cols) and r (n×n, upper-triangular).
 * @note O(n³); allocates both members.
 * @test LinalgRoutines.CholeskyAndQR
 */
QR qr(const NDArray& a);
/** Result of svd(): A = u·diag(s)·vh. */
struct SVD {
    NDArray u;   ///< Left singular vectors, m×n.
    NDArray s;   ///< Singular values in descending order (length n).
    NDArray vh;  ///< Right singular vectors transposed, n×n (the Vᵀ in A = u·diag(s)·Vᵀ).
};
/**
 * Singular value decomposition (one-sided Jacobi; requires rows ≥ cols).
 * @param a m×n matrix.
 * @return @ref SVD with u (m×n), s (descending singular values), vh (n×n = Vᵀ).
 * @note iterative O(n³); allocates all members.
 * @test LinalgRoutines.SvdAndEigh
 */
SVD svd(const NDArray& a);

// ---- Matrix eigenvalues ----
/** Result of eig() / eigh(): column j of vectors is the eigenvector for values[j]. */
struct Eig {
    NDArray values;   ///< Eigenvalues (length n).
    NDArray vectors;  ///< Eigenvectors as columns: column j matches values[j] (empty if not computed).
};
/**
 * Eigen-decomposition of a general square matrix (real spectrum only; throws on complex pairs).
 * @param a square matrix.
 * @return @ref Eig (vectors empty unless @p a is symmetric).
 * @note iterative O(n³) via Hessenberg + shifted QR; allocates both members.
 * @test LinalgRoutines.GeneralEig
 */
Eig eig(const NDArray& a);                                // general square matrix
/**
 * Eigenvalues of a general square matrix, descending (real only; throws on complex pairs).
 * @param a square matrix.
 * @return length-n vector of eigenvalues.
 * @note iterative O(n³); allocates a new NDArray result.
 * @test LinalgRoutines.SvdAndEigh
 */
NDArray eigvals(const NDArray& a);
/**
 * Eigen-decomposition of a symmetric matrix (cyclic Jacobi).
 * @param a square symmetric matrix.
 * @return @ref Eig with descending values and matching eigenvectors.
 * @note iterative O(n³); allocates both members.
 * @test LinalgRoutines.SvdAndEigh
 */
Eig eigh(const NDArray& a);                               // symmetric / Hermitian
/**
 * Eigenvalues of a symmetric matrix, descending (cyclic Jacobi).
 * @param a square symmetric matrix.
 * @return length-n vector of eigenvalues.
 * @note iterative O(n³); allocates a new NDArray result.
 * @test LinalgRoutines.EigvalshSymmetric
 */
NDArray eigvalsh(const NDArray& a);

// ---- Norms and other numbers ----
/**
 * Norm: L2 for vectors, Frobenius for matrices.
 * @param a vector or matrix.
 * @return √Σ xᵢ².
 * @note O(n) for vectors / O(n²) for matrices; copies into scratch, returns a double.
 * @test LinalgRoutines.NormAndRank
 */
double norm(const NDArray& a);                            // default: Frobenius / L2
/**
 * 2-norm condition number σ_max/σ_min (∞ if singular).
 * @param a matrix.
 * @return condition number.
 * @note iterative O(n³) via SVD; allocates scratch O(n²) for the factorization, returns a
 *   double.
 * @test LinalgRoutines.SlogdetAndCond
 */
double cond(const NDArray& a);
/**
 * Determinant via LU with partial pivoting.
 * @param a square matrix.
 * @return det(A).
 * @note O(n³); allocates scratch O(n²) for the factorization, returns a double.
 * @test LinalgRoutines.SolveDetInv
 */
double det(const NDArray& a);
/**
 * Numerical rank from SVD singular-value thresholding.
 * @param a matrix.
 * @return rank.
 * @note iterative O(n³) via SVD; allocates scratch O(n²) for the factorization.
 * @test LinalgRoutines.NormAndRank
 */
long long matrix_rank(const NDArray& a);
/** Result of slogdet(): det(A) = sign·exp(logabsdet). */
struct SLogDet {
    double sign;       ///< Sign of the determinant (−1, 0, or +1).
    double logabsdet;  ///< Natural log of |det(A)|, so det(A) = sign·exp(logabsdet).
};
/**
 * Sign and log|det| via LU (overflow-safe determinant).
 * @param a square matrix.
 * @return @ref SLogDet.
 * @note O(n³); allocates scratch O(n²) for the factorization (the struct members are plain
 *   doubles).
 * @test LinalgRoutines.SlogdetAndCond
 */
SLogDet slogdet(const NDArray& a);
/**
 * Trace: sum of the main diagonal.
 * @param a matrix.
 * @return Σ aᵢᵢ.
 * @note O(n) summation, but copies the matrix into scratch O(n²) first; returns a double.
 * @test LinalgRoutines.ProductsAndTrace
 */
double trace(const NDArray& a);

// ---- Solving equations and inverting matrices ----
/**
 * Solve A·x = b via LU with partial pivoting.
 * @param a square coefficient matrix.
 * @param b right-hand-side vector.
 * @return solution x.
 * @note O(n³); allocates a new NDArray result.
 * @test LinalgRoutines.SolveDetInv
 */
NDArray solve(const NDArray& a, const NDArray& b);        // A x = b
/**
 * Least-squares solution min‖A·x − b‖ (computed as @ref pinv (a)·b).
 * @param a m×n matrix.
 * @param b right-hand side.
 * @return minimizing x.
 * @note iterative O(n³) via SVD; allocates a new NDArray result.
 * @test LinalgRoutines.Lstsq
 */
NDArray lstsq(const NDArray& a, const NDArray& b);        // least-squares solution
/**
 * Matrix inverse via LU with partial pivoting.
 * @param a square matrix.
 * @return A⁻¹.
 * @note O(n³); allocates a new NDArray result.
 * @test LinalgRoutines.SolveDetInv
 */
NDArray inv(const NDArray& a);
/**
 * Moore–Penrose pseudo-inverse via SVD (any shape).
 * @param a m×n matrix.
 * @return n×m pseudo-inverse.
 * @note iterative O(n³) via SVD; allocates a new NDArray result.
 * @test LinalgRoutines.PinvCondRankOnWideMatrix
 */
NDArray pinv(const NDArray& a);                           // Moore–Penrose pseudo-inverse

} // namespace cheatah::linalg
