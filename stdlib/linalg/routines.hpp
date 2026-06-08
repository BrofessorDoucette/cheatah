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
 * SIMD here is pure compiler auto-vectorization (no intrinsics). On a scalar build
 * (no vector ISA) every routine still returns identical results, just slower — see
 * simd.hpp's file comment for the full SIMD model, the no-SIMD behavior, and the
 * compile-time-dispatch limitation.
 *
 * @note `n` below is the matrix dimension. The general eigensolvers `eig`/`eigvals`
 *       return a **complex** spectrum (@ref CNDArray) — a real matrix can have
 *       complex conjugate eigenvalue pairs — while the Hermitian solvers
 *       `eigh`/`eigvalsh` return a guaranteed-real spectrum. Routines that extract
 *       a working copy "allocate scratch O(n²) for the factorization" even when
 *       they return a scalar.
 */
#include <complex>
#include <vector>

#include "ndarray.hpp"

namespace cheatah::linalg {

// The routines operate on cheatah::ndarray::NDArray, re-exported unqualified for brevity in the
// signatures below. The directive below hides this re-export from the API doc generator so it
// does not emit a phantom duplicate cheatah::linalg::NDArray class in the namespace/XML structure.
/// \cond INTERNAL
using ndarray::NDArray;
/// \endcond

/// A complex scalar (`std::complex<double>`) — the element type of @ref CNDArray and
/// the return type of the complex inner products @ref dot / @ref vdot.
using Cplx = std::complex<double>;

/// A complex array (`basic_ndarray<std::complex<double>>`) — what the general
/// eigensolvers return, since a real matrix can have complex eigenvalues. Prints
/// element-wise as `a+bj` via @ref cheatah::ndarray::to_string.
using CNDArray = ndarray::basic_ndarray<Cplx>;

// ---- Matrix and vector products ----
/**
 * Dot product: 1-D inner product (vectors flattened).
 *
 * Flattens each operand to a vector (1-D, or 2-D with a size-1 row/column) and
 * sums the elementwise products; throws if either is not vector-shaped or the
 * lengths differ.
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @complexity O(n).
 * @alloc copies both operands into scratch O(n); returns a double.
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Dot
 * @systest StdlibE2E.Linalg
 */
double dot(const NDArray& a, const NDArray& b);          // 1-D dot / 2-D matmul
/**
 * Vector dot product (alias of @ref dot; flattens N×1/1×N).
 *
 * Delegates directly to @ref dot; for real vectors there is no conjugation, so this
 * is identical to @ref dot and @ref inner. The **complex** overload differs — it is
 * conjugate-linear (see the `CNDArray` `vdot` below).
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @complexity O(n).
 * @alloc scratch O(n); returns a double.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Vdot
 * @systest StdlibE2E.Linalg
 */
double vdot(const NDArray& a, const NDArray& b);
/**
 * Inner product of two vectors (alias of @ref dot).
 *
 * Delegates directly to @ref dot, so the same vector-shape and equal-length
 * requirements apply.
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @complexity O(n).
 * @alloc scratch O(n); returns a double.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Inner
 * @systest StdlibE2E.Linalg
 */
double inner(const NDArray& a, const NDArray& b);
/**
 * Outer product of two vectors.
 *
 * Flattens both operands to vectors and forms the full rank-1 matrix; any pair
 * of vector lengths is accepted (no matching constraint).
 * @param a length-n vector.
 * @param b length-m vector.
 * @return n×m matrix aᵢbⱼ.
 * @complexity O(n·m).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Outer
 * @systest StdlibE2E.Linalg
 */
NDArray outer(const NDArray& a, const NDArray& b);
/**
 * Matrix multiply.
 *
 * Requires both operands to be 2-D with matching inner dimensions (a's cols ==
 * b's rows), throwing on a mismatch or a non-2-D input; the inner loop runs in
 * ikj order so the contiguous accumulation auto-vectorizes.
 * @param a m×k matrix.
 * @param b k×p matrix.
 * @return m×p product.
 * @complexity O(n³).
 * @alloc allocates a new NDArray result (plus scratch copies of @p a and @p b).
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Matmul
 * @systest StdlibE2E.Linalg
 */
NDArray matmul(const NDArray& a, const NDArray& b);

// ---- complex products (complex inner-product spaces) ----
/**
 * Bilinear dot product of two **complex** vectors: Σ aᵢbᵢ (no conjugation, matching
 * numpy's `dot`). For the Hermitian inner product ⟨a, b⟩ use @ref vdot.
 * @param a,b same-length complex vectors.
 * @return Σ aᵢbᵢ as a `std::complex<double>`; throws on a length mismatch.
 * @complexity O(n).
 * @alloc scratch O(n); returns a complex scalar.
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ComplexDot
 * @systest StdlibE2E.LinalgComplex
 */
Cplx dot(const CNDArray& a, const CNDArray& b);
/**
 * Conjugate-linear (Hermitian) inner product of two **complex** vectors:
 * ⟨a, b⟩ = Σ conj(aᵢ)·bᵢ — the inner product of a complex vector space, conjugating
 * the FIRST argument (numpy's `vdot` convention). `vdot(a, a)` is the real squared
 * norm ‖a‖². For the non-conjugating product use @ref dot.
 * @param a,b same-length complex vectors.
 * @return Σ conj(aᵢ)·bᵢ as a `std::complex<double>`; throws on a length mismatch.
 * @complexity O(n).
 * @alloc scratch O(n); returns a complex scalar.
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ComplexVdot
 * @systest StdlibE2E.LinalgComplex
 */
Cplx vdot(const CNDArray& a, const CNDArray& b);
/**
 * Matrix multiply of two **complex** matrices (the complex analogue of @ref matmul).
 * @param a m×k complex matrix.
 * @param b k×p complex matrix.
 * @return m×p complex product; throws on an inner-dimension mismatch or non-2-D input.
 * @complexity O(n³).
 * @alloc allocates a new CNDArray result (plus scratch copies of @p a and @p b).
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ComplexMatmul
 * @systest StdlibE2E.LinalgComplex
 */
CNDArray matmul(const CNDArray& a, const CNDArray& b);
/**
 * Conjugate transpose (Hermitian adjoint) Aᴴ of a **complex** matrix: transpose,
 * then conjugate every entry. A matrix is Hermitian iff `conj_transpose(A) == A`.
 * @param a a 2-D complex matrix.
 * @return the c×r adjoint of an r×c input; throws on non-2-D input.
 * @complexity O(r·c).
 * @alloc allocates a new CNDArray result.
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ConjTranspose
 * @systest StdlibE2E.LinalgComplex
 */
CNDArray conj_transpose(const CNDArray& a);
/**
 * Integer matrix power Aⁿ (negative n via @ref inv).
 *
 * Requires a square matrix (throws otherwise); n == 0 returns the identity, and
 * negative n first inverts @p a via @ref inv (so it inherits @ref inv's
 * singular-matrix behavior) before raising to |n|.
 * @param a square matrix.
 * @param n exponent.
 * @return Aⁿ.
 * @complexity O(n³·log|n|) by binary exponentiation.
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.MatrixPower
 * @crtest LinalgCompileRun.MatrixPower
 * @systest StdlibE2E.Linalg
 */
NDArray matrix_power(const NDArray& a, long long n);
/**
 * Kronecker product.
 *
 * Requires both operands to be 2-D (throws otherwise) and replaces each entry of
 * @p a with that scalar times the whole of @p b, giving the (m·p)×(k·q) block
 * matrix; no dimension matching is needed.
 * @param a m×k matrix.
 * @param b p×q matrix.
 * @return (m·p)×(k·q) block product.
 * @complexity O(n⁴) in the output area.
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Kron
 * @systest StdlibE2E.Linalg
 */
NDArray kron(const NDArray& a, const NDArray& b);

// ---- Decompositions ----
/**
 * Cholesky factor of a symmetric positive-definite matrix (throws otherwise).
 *
 * Requires a square matrix and computes L column by column reading only the
 * lower triangle of @p a; if any pivot (the diagonal under the square root) is
 * non-positive it throws "matrix is not positive-definite", which also catches
 * non-SPD or non-symmetric input.
 * @param a square SPD matrix.
 * @return lower-triangular L with A = L·Lᵀ.
 * @complexity O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.CholeskyAndQR
 * @crtest LinalgCompileRun.Cholesky
 * @systest StdlibE2E.Linalg
 */
NDArray cholesky(const NDArray& a);                       // lower-triangular L (A = L Lᵀ)
/** Result of qr(): A = q·r with orthonormal q and upper-triangular r. */
struct QR {
    NDArray q;  ///< Orthonormal columns, m×n (the Q in A = Q·R).
    NDArray r;  ///< Upper-triangular factor, n×n (the R in A = Q·R).
};
/**
 * Reduced QR via Householder reflections (requires rows ≥ cols).
 *
 * Applies successive Householder reflectors to triangularize @p a, returning the
 * thin/reduced factors; throws "qr requires rows >= cols" for wide matrices.
 * Rank-deficient columns (zero pivot norm) are skipped, leaving the
 * corresponding R entries zero.
 * @param a m×n matrix.
 * @return @ref QR with q (m×n, orthonormal cols) and r (n×n, upper-triangular).
 * @complexity O(n³).
 * @alloc allocates both members.
 * @test LinalgRoutines.CholeskyAndQR
 * @crtest LinalgCompileRun.Qr
 * @systest StdlibE2E.Linalg
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
 *
 * Iterates one-sided Jacobi rotations (capped at 80 sweeps) to orthogonalize the
 * columns, then normalizes them into u and sorts the singular values descending;
 * throws "svd requires rows >= cols" for wide matrices (transpose first). Exact
 * zero singular values yield a zero u column.
 * @param a m×n matrix.
 * @return @ref SVD with u (m×n), s (descending singular values), vh (n×n = Vᵀ).
 * @complexity iterative O(n³).
 * @alloc allocates all members.
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Svd
 * @systest StdlibE2E.Linalg
 */
SVD svd(const NDArray& a);

// ---- Matrix eigenvalues ----
/** Result of eigh(): a real spectrum — column j of vectors is the eigenvector for values[j]. */
struct Eig {
    NDArray values;   ///< Eigenvalues (length n), real.
    NDArray vectors;  ///< Eigenvectors as columns: column j matches values[j] (empty if not computed).
};
/**
 * Result of the general eig(): a **complex** spectrum, since a real matrix can have
 * complex conjugate eigenvalue pairs. Column j of vectors is the eigenvector for values[j].
 */
struct EigC {
    CNDArray values;   ///< Eigenvalues (length n), complex.
    CNDArray vectors;  ///< Eigenvectors as columns: column j matches values[j].
};
/**
 * Eigen-decomposition of a general square matrix (**complex** spectrum and
 * eigenvectors).
 *
 * For a symmetric @p a it delegates to @ref eigh (promoted to complex with zero
 * imaginary part); otherwise it uses Hessenberg reduction + shifted QR for the
 * eigenvalues, then **inverse iteration** for each eigenvector. A real matrix with a
 * complex conjugate pair (e.g. a rotation) yields those complex eigenvalues and
 * eigenvectors rather than throwing. Throws on a non-square matrix or if the QR
 * iteration fails to converge.
 * @param a square matrix.
 * @return @ref EigC with complex values and matching complex eigenvector columns.
 * @complexity iterative O(n³) via Hessenberg + shifted QR.
 * @alloc allocates both members.
 * @test LinalgRoutines.GeneralEig
 * @crtest LinalgCompileRun.Eig
 * @systest StdlibE2E.Linalg
 */
EigC eig(const NDArray& a);                               // general square matrix
/**
 * Eigenvalues of a general square matrix (**complex**), descending.
 *
 * Routes symmetric input through cyclic Jacobi and everything else through
 * Hessenberg + shifted QR, then sorts the result descending (by real part, then by
 * imaginary part). A real matrix with a complex conjugate pair yields those complex
 * eigenvalues rather than throwing. Throws on a non-square matrix or non-convergence
 * of the QR iteration.
 * @param a square matrix.
 * @return length-n complex vector of eigenvalues.
 * @complexity iterative O(n³).
 * @alloc allocates a new CNDArray result.
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Eigvals
 * @systest StdlibE2E.Linalg
 */
CNDArray eigvals(const NDArray& a);
/**
 * Eigen-decomposition of a symmetric matrix (cyclic Jacobi).
 *
 * Runs cyclic Jacobi (capped at 100 sweeps) to diagonalize @p a, returning real
 * eigenvalues sorted descending with matching eigenvector columns; it reads the
 * full matrix and assumes symmetry rather than checking it, so asymmetric input
 * yields meaningless results. Throws on a non-square matrix.
 * @param a square symmetric matrix.
 * @return @ref Eig with descending values and matching eigenvectors.
 * @complexity iterative O(n³).
 * @alloc allocates both members.
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Eigh
 * @systest StdlibE2E.Linalg
 */
Eig eigh(const NDArray& a);                               // symmetric / Hermitian
/**
 * Eigenvalues of a symmetric matrix, descending (cyclic Jacobi).
 *
 * Same cyclic-Jacobi diagonalization as @ref eigh but discards the eigenvectors;
 * assumes (does not verify) symmetry and throws on a non-square matrix.
 * @param a square symmetric matrix.
 * @return length-n vector of eigenvalues.
 * @complexity iterative O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.EigvalshSymmetric
 * @crtest LinalgCompileRun.Eigvalsh
 * @systest StdlibE2E.Linalg
 */
NDArray eigvalsh(const NDArray& a);
/**
 * Result of the complex Hermitian eigh(): **real** eigenvalues (a Hermitian operator
 * has a real spectrum) with **complex** eigenvectors. Column j of vectors is the
 * (unit-norm) eigenvector for values[j].
 */
struct EighC {
    NDArray values;    ///< Eigenvalues (length n), real and descending.
    CNDArray vectors;  ///< Eigenvectors as columns: column j matches values[j].
};
/**
 * Eigen-decomposition of a **complex Hermitian** matrix — real eigenvalues, complex
 * eigenvectors.
 *
 * The quantum-mechanics workhorse: a Hermitian operator (`conj_transpose(a) == a`)
 * has a guaranteed-real spectrum and orthonormal complex eigenvectors. Computed by
 * the real symmetric Jacobi solver on the 2n×2n real embedding
 * `[[Re a, -Im a], [Im a, Re a]]`. Assumes (does not verify) that @p a is Hermitian;
 * throws on a non-square matrix.
 * @param a square complex Hermitian matrix.
 * @return @ref EighC with descending real values and matching complex eigenvectors.
 * @complexity iterative O(n³).
 * @alloc allocates both members.
 * @test LinalgRoutines.ComplexHermitianEigh
 * @crtest LinalgCompileRun.EighComplex
 * @systest StdlibE2E.LinalgComplex
 */
EighC eigh(const CNDArray& a);
/**
 * Eigenvalues of a **complex Hermitian** matrix, descending and **real**.
 *
 * Same 2n-embedding Jacobi diagonalization as the complex @ref eigh but discards the
 * eigenvectors; assumes (does not verify) Hermitian and throws on a non-square matrix.
 * @param a square complex Hermitian matrix.
 * @return length-n real vector of eigenvalues.
 * @complexity iterative O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.ComplexHermitianEigh
 * @crtest LinalgCompileRun.EigvalshComplex
 * @systest StdlibE2E.LinalgComplex
 */
NDArray eigvalsh(const CNDArray& a);

// ---- Norms and other numbers ----
/**
 * Norm: L2 for vectors, Frobenius for matrices.
 *
 * Dispatches on rank: 1-D (or lower) inputs get the Euclidean L2 norm, 2-D
 * inputs the Frobenius norm; either way it is the square root of the sum of
 * squared entries.
 * @param a vector or matrix.
 * @return √Σ xᵢ².
 * @complexity O(n) for vectors / O(n²) for matrices.
 * @alloc copies into scratch; returns a double.
 * @test LinalgRoutines.NormAndRank
 * @crtest LinalgCompileRun.Norm
 * @systest StdlibE2E.Linalg
 */
double norm(const NDArray& a);                            // default: Frobenius / L2
/**
 * 2-norm condition number σ_max/σ_min (∞ if singular).
 *
 * Takes the ratio of largest to smallest singular value from a Jacobi SVD
 * (transposing internally for wide matrices); returns +infinity when the
 * smallest singular value is exactly zero (singular/rank-deficient).
 * @param a matrix.
 * @return condition number.
 * @complexity iterative O(n³) via SVD.
 * @alloc allocates scratch O(n²) for the factorization; returns a double.
 * @test LinalgRoutines.SlogdetAndCond
 * @crtest LinalgCompileRun.Cond
 * @systest StdlibE2E.Linalg
 */
double cond(const NDArray& a);
/**
 * Determinant via LU with partial pivoting.
 *
 * Computes the product of the LU pivots times the permutation sign; requires a
 * square matrix (throws otherwise). A singular matrix yields a determinant of
 * (or extremely near) zero rather than an error.
 * @param a square matrix.
 * @return det(A).
 * @complexity O(n³).
 * @alloc allocates scratch O(n²) for the factorization; returns a double.
 * @test LinalgRoutines.SolveDetInv
 * @crtest LinalgCompileRun.Det
 * @systest StdlibE2E.Linalg
 */
double det(const NDArray& a);
/**
 * Numerical rank from SVD singular-value thresholding.
 *
 * Counts singular values above a tolerance scaled by the largest singular value
 * and the matrix size (the standard numpy-style threshold); accepts any shape,
 * transposing wide matrices internally.
 * @param a matrix.
 * @return rank.
 * @complexity iterative O(n³) via SVD.
 * @alloc allocates scratch O(n²) for the factorization.
 * @test LinalgRoutines.NormAndRank
 * @crtest LinalgCompileRun.MatrixRank
 * @systest StdlibE2E.Linalg
 */
long long matrix_rank(const NDArray& a);
/** Result of slogdet(): det(A) = sign·exp(logabsdet). */
struct SLogDet {
    double sign;       ///< Sign of the determinant (−1, 0, or +1).
    double logabsdet;  ///< Natural log of |det(A)|, so det(A) = sign·exp(logabsdet).
};
/**
 * Sign and log|det| via LU (overflow-safe determinant).
 *
 * Sums the logs of the absolute LU pivots (avoiding the over/underflow of a raw
 * product) and tracks the sign from the pivot signs and permutation parity;
 * requires a square matrix (throws otherwise). A singular matrix gives a hugely
 * negative logabsdet rather than −infinity, since a zero pivot is nudged to a
 * tiny value during factorization.
 * @param a square matrix.
 * @return @ref SLogDet.
 * @complexity O(n³).
 * @alloc allocates scratch O(n²) for the factorization (the struct members are plain doubles).
 * @test LinalgRoutines.SlogdetAndCond
 * @crtest LinalgCompileRun.Slogdet
 * @systest StdlibE2E.Linalg
 */
SLogDet slogdet(const NDArray& a);
/**
 * Trace: sum of the main diagonal.
 *
 * Sums the diagonal entries up to min(rows, cols), so it also works on
 * non-square matrices; requires a 2-D input (throws otherwise).
 * @param a matrix.
 * @return Σ aᵢᵢ.
 * @complexity O(n) summation.
 * @alloc copies the matrix into scratch O(n²) first; returns a double.
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Trace
 * @systest StdlibE2E.Linalg
 */
double trace(const NDArray& a);

// ---- Solving equations and inverting matrices ----
/**
 * Solve A·x = b via LU with partial pivoting.
 *
 * Factorizes @p a once then does forward/back substitution against @p b;
 * requires @p a square and @p b a vector of matching length (throws otherwise).
 * A singular @p a does not throw but yields a garbage/overflowing solution
 * (pivots are nudged off zero rather than detected).
 * @param a square coefficient matrix.
 * @param b right-hand-side vector.
 * @return solution x.
 * @complexity O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.SolveDetInv
 * @crtest LinalgCompileRun.Solve
 * @systest StdlibE2E.Linalg
 */
NDArray solve(const NDArray& a, const NDArray& b);        // A x = b
/**
 * Least-squares solution min‖A·x − b‖ (computed as @ref pinv (a)·b).
 *
 * Forms the Moore–Penrose pseudo-inverse via SVD and multiplies it by @p b, so
 * it handles over- and under-determined systems and returns the minimum-norm
 * solution for rank-deficient @p a; @p b must be conformable for the
 * @ref matmul step.
 * @param a m×n matrix.
 * @param b right-hand side.
 * @return minimizing x.
 * @complexity iterative O(n³) via SVD.
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.Lstsq
 * @crtest LinalgCompileRun.Lstsq
 * @systest StdlibE2E.Linalg
 */
NDArray lstsq(const NDArray& a, const NDArray& b);        // least-squares solution
/**
 * Matrix inverse via LU with partial pivoting.
 *
 * Factorizes @p a once and back-solves against each identity column; requires a
 * square matrix (throws otherwise). A singular @p a does not throw but produces
 * garbage/overflowing entries since zero pivots are nudged rather than detected.
 * @param a square matrix.
 * @return A⁻¹.
 * @complexity O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.SolveDetInv
 * @crtest LinalgCompileRun.Inv
 * @systest StdlibE2E.Linalg
 */
NDArray inv(const NDArray& a);
/**
 * Moore–Penrose pseudo-inverse via SVD (any shape).
 *
 * Computes V·diag(1/σ)·Uᵀ from a Jacobi SVD, transposing wide matrices
 * internally so any shape works; singular values at or below a size-scaled
 * tolerance are dropped (treated as zero) so it stays well-defined for
 * rank-deficient input.
 * @param a m×n matrix.
 * @return n×m pseudo-inverse.
 * @complexity iterative O(n³) via SVD.
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.PinvCondRankOnWideMatrix
 * @crtest LinalgCompileRun.Pinv
 * @systest StdlibE2E.Linalg
 */
NDArray pinv(const NDArray& a);                           // Moore–Penrose pseudo-inverse

} // namespace cheatah::linalg
