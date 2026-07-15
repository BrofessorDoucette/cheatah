// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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
 * Golub–Reinsch SVD, Householder-tridiagonal + QL symmetric eigen, Hessenberg + shifted-QR general
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
 *       `eigh`/`eigvalsh` return a guaranteed-real spectrum. The LU/SVD-based scalar
 *       routines (`det`/`slogdet`/`cond`/`matrix_rank`) allocate scratch O(n²) for the
 *       factorization even though they return a scalar; the products and reductions
 *       (`dot`/`matmul`/`trace`/`norm`/…) read their operands in place — zero-copy
 *       when contiguous, packing a strided view once.
 */
#include <complex>
#include <vector>

#include "backend.hpp"
#include "concepts.hpp"
#include "ndarray.hpp"

namespace cheatah::linalg {

// The routines operate on cheatah::ndarray::NDArray, re-exported unqualified for brevity in the
// signatures below. The directive below hides this re-export from the API doc generator so it
// does not emit a phantom duplicate cheatah::linalg::NDArray class in the namespace/XML structure.
/// @cond INTERNAL
using ndarray::NDArray;
/// @endcond

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
 * @alloc none for contiguous operands (read in place); a non-contiguous view is
 *        packed once into scratch O(n). Returns a double.
 * @test LinalgRoutines.ProductsAndTrace
 * @crtest LinalgCompileRun.Dot
 * @systest StdlibE2E.Linalg
 */
double dot(const NDArray& a, const NDArray& b);          // flattened 1-D inner product
/**
 * Vector dot product (alias of @ref dot; flattens N×1/1×N).
 *
 * Delegates directly to @ref dot; for real vectors there is no conjugation, so this
 * is identical to @ref dot and @ref inner. The **complex** overload differs — it is
 * conjugate-linear (see the `CNDArray` `vdot` below).
 * @param a,b same-length vectors.
 * @return Σ aᵢbᵢ.
 * @complexity O(n).
 * @alloc none for contiguous operands; non-contiguous packs once O(n). Returns a double.
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
 * @alloc none for contiguous operands; non-contiguous packs once O(n). Returns a double.
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
 * @alloc allocates only the n×m result; operands read in place when contiguous.
 * @test LinalgRoutines.VdotInnerOuterKron
 * @crtest LinalgCompileRun.Outer
 * @systest StdlibE2E.Linalg
 */
NDArray outer(const NDArray& a, const NDArray& b);
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Outer product into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref outer, writing the rank-1 result straight into @p out with no allocation.
 * @param out destination; a contiguous n×m matrix, overwritten. Must NOT alias @p a or @p b.
 * @param a length-n vector.
 * @param b length-m vector.
 * @complexity O(n·m).
 * @alloc none for contiguous operands (result written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.OuterIntoReusesBuffer
 */
void outer(NDArray& out, const NDArray& a, const NDArray& b);
/// @endcond
// The allocating matmul (m×k · k×p → m×p) is the concept-templated generic front
// `matmul(const A&, const B&)` in backend.hpp — one template serving host and device
// operands; the host path routes through the out-parameter kernel declared just below.
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Matmul into the CALLER'S buffer @p out (out FIRST, like assignment) — the user-provided-output
 * overload, NO result allocation (a hot loop, e.g. an MLP layer, hands the same scratch every call).
 * @param out the destination; a contiguous [a.rows, b.cols] matrix, overwritten. Must NOT alias @p a or
 *        @p b — matmul reads all of both while writing out, so it is not an in-place op.
 * @param a left matrix.
 * @param b right matrix.
 * @complexity O(ar·ac·bc).
 * @alloc none for contiguous operands (product written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.MatmulIntoReusesBuffer
 */
void matmul(NDArray& out, const NDArray& a, const NDArray& b);
/// @endcond

// ---- complex products (complex inner-product spaces) ----
/**
 * Bilinear dot product of two **complex** vectors: Σ aᵢbᵢ (no conjugation, matching
 * numpy's `dot`). For the Hermitian inner product ⟨a, b⟩ use @ref vdot.
 * @param a,b same-length complex vectors.
 * @return Σ aᵢbᵢ as a `std::complex<double>`; throws on a length mismatch.
 * @complexity O(n).
 * @alloc none for contiguous operands (read in place); a non-contiguous view packs
 *        once O(n). Returns a complex scalar.
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
 * @alloc none for contiguous operands (read in place); a non-contiguous view packs
 *        once O(n). Returns a complex scalar.
 * @test LinalgRoutines.ComplexProducts
 * @crtest LinalgCompileRun.ComplexVdot
 * @systest StdlibE2E.LinalgComplex
 */
Cplx vdot(const CNDArray& a, const CNDArray& b);
// Complex matmul (the complex analogue) is served by the SAME generic `matmul` front in
// backend.hpp: for CNDArray operands it instantiates the complex path (a compile-time branch
// on the element type), routing to the complex out-parameter kernel declared just below.
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Complex matmul into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of the
 * complex @ref matmul, writing the product straight into @p out with no allocation.
 * @param out destination; a contiguous [a.rows, b.cols] complex matrix, overwritten. Must NOT
 *        alias @p a or @p b (it reads all of both while writing out — not an in-place op).
 * @param a m×k complex matrix.
 * @param b k×p complex matrix.
 * @complexity O(n³).
 * @alloc none for contiguous operands (product written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.ComplexMatmulIntoReusesBuffer
 */
void matmul(CNDArray& out, const CNDArray& a, const CNDArray& b);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Conjugate transpose into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref conj_transpose, writing the c×r adjoint straight into @p out with no allocation.
 * @param out destination; a contiguous c×r complex matrix (for an r×c input), overwritten. Must
 *        NOT alias @p a (it reads A while writing the transpose — not an in-place op).
 * @param a a 2-D complex matrix.
 * @complexity O(r·c).
 * @alloc none for a contiguous operand (adjoint written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.ConjTransposeIntoReusesBuffer
 */
void conj_transpose(CNDArray& out, const CNDArray& a);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Matrix power into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref matrix_power.
 * @param out destination; a contiguous n×n matrix, overwritten with Aⁿ.
 * @param a square matrix.
 * @param n exponent.
 * @complexity O(n³·log|n|).
 * @alloc reuses @p out; the binary-exponentiation products allocate their own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void matrix_power(NDArray& out, const NDArray& a, long long n);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Kronecker product into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref kron, writing the block product straight into @p out with no allocation.
 * @param out destination; a contiguous (m·p)×(k·q) matrix, overwritten. Must NOT alias @p a or @p b.
 * @param a m×k matrix.
 * @param b p×q matrix.
 * @complexity O(n⁴) in the output area.
 * @alloc none for contiguous operands (block product written straight into @p out); a
 *        non-contiguous operand is packed once into scratch.
 * @test LinalgRoutines.KronIntoReusesBuffer
 */
void kron(NDArray& out, const NDArray& a, const NDArray& b);
/// @endcond

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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Cholesky factor into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref cholesky.
 * @param out destination; a contiguous n×n matrix, overwritten with the lower-triangular L.
 * @param a square SPD matrix.
 * @complexity O(n³).
 * @alloc reuses @p out (the factor is computed into private scratch, then copied in).
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void cholesky(NDArray& out, const NDArray& a);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Reduced QR into the caller's buffers (outs FIRST) — the buffer-reuse overload of @ref qr,
 * filling @p q and @p r instead of allocating a @ref QR.
 * @param q destination for the orthonormal factor; a contiguous m×n matrix, overwritten.
 * @param r destination for the upper-triangular factor; a contiguous n×n matrix, overwritten.
 * @param a m×n matrix.
 * @complexity O(n³).
 * @alloc reuses @p q and @p r (the factors are computed into private scratch, then copied in).
 * @test LinalgRoutines.DecompositionOutReusesBuffer
 */
void qr(NDArray& q, NDArray& r, const NDArray& a);
/// @endcond
/** Result of svd(): A = u·diag(s)·vh. */
struct SVD {
    NDArray u;   ///< Left singular vectors, m×n.
    NDArray s;   ///< Singular values in descending order (length n).
    NDArray vh;  ///< Right singular vectors transposed, n×n (the Vᵀ in A = u·diag(s)·Vᵀ).
};
/**
 * Singular value decomposition (Golub–Reinsch; requires rows ≥ cols).
 *
 * Reduces @p a to upper-bidiagonal form by Householder reflections, then diagonalizes
 * it with implicit-shift QR (accumulating U and V), and sorts the singular values
 * descending — the world-standard dense SVD (what LAPACK's dgesvd reduces to). Throws
 * "svd requires rows >= cols" for wide matrices (transpose first); singular values come
 * out non-negative.
 * @param a m×n matrix.
 * @return @ref SVD with u (m×n), s (descending singular values), vh (n×n = Vᵀ).
 * @complexity iterative O(n³).
 * @alloc allocates all members.
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Svd
 * @systest StdlibE2E.Linalg
 */
SVD svd(const NDArray& a);
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Full SVD into the caller's buffers (outs FIRST) — the buffer-reuse overload of @ref svd,
 * filling @p u, @p s and @p vh instead of allocating an @ref SVD.
 * @param u destination for the left singular vectors; a contiguous m×n matrix, overwritten.
 * @param s destination for the singular values; a contiguous length-n vector, overwritten.
 * @param vh destination for Vᵀ; a contiguous n×n matrix, overwritten.
 * @param a m×n matrix (rows ≥ cols).
 * @complexity iterative O(n³).
 * @alloc reuses @p u, @p s, @p vh (the factors are computed into private scratch, then copied in).
 * @test LinalgRoutines.DecompositionOutReusesBuffer
 */
void svd(NDArray& u, NDArray& s, NDArray& vh, const NDArray& a);
/// @endcond
/**
 * Singular values only (≈ `numpy.linalg.svd(a, compute_uv=False)` / `svdvals`).
 *
 * Runs the same Golub–Reinsch reduction as @ref svd but takes the **values-only** fast
 * path — it never accumulates U or V, and skips the (dominant) U/V Givens rotations in
 * the QR sweep — so it is several times faster than the full decomposition. Accepts any
 * shape (singular values of `a` and `aᵀ` coincide).
 * @param a m×n matrix.
 * @return length-min(m,n) vector of singular values, descending.
 * @complexity iterative O(n³), but a large constant factor below @ref svd.
 * @alloc allocates a new NDArray result (no U/V scratch).
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Svdvals
 * @systest StdlibE2E.Linalg
 */
NDArray svdvals(const NDArray& a);
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Singular values into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref svdvals.
 * @param out destination; a contiguous length-min(m,n) vector, overwritten with the descending values.
 * @param a m×n matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the Golub–Reinsch reduction allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void svdvals(NDArray& out, const NDArray& a);
/// @endcond

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
 * @complexity iterative O(n³) via Hessenberg + shifted QR for the eigenvalues; the
 *        general (non-symmetric) eigenvectors add O(n⁴) — one inverse iteration, each
 *        with its own O(n³) complex LU factorization, per eigenvalue (a symmetric @p a
 *        stays O(n³) via @ref eigh, which accumulates the vectors in the QL sweep).
 * @alloc allocates both members.
 * @test LinalgRoutines.GeneralEig
 * @crtest LinalgCompileRun.Eig
 * @systest StdlibE2E.Linalg
 */
EigC eig(const NDArray& a);                               // general square matrix
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * General eigendecomposition into the caller's buffers (outs FIRST) — the buffer-reuse overload of
 * @ref eig, filling @p values and @p vectors instead of allocating an @ref EigC.
 * @param values destination for the complex eigenvalues; a contiguous length-n vector, overwritten.
 * @param vectors destination for the complex eigenvectors (columns); a contiguous n×n matrix, overwritten.
 * @param a square matrix.
 * @complexity iterative O(n³) for the eigenvalues; general (non-symmetric) eigenvectors
 *        add O(n⁴) (inverse iteration per eigenvalue — see @ref eig).
 * @alloc reuses @p values and @p vectors (computed into private scratch, then copied in).
 * @test LinalgRoutines.DecompositionOutReusesBuffer
 */
void eig(CNDArray& values, CNDArray& vectors, const NDArray& a);
/// @endcond
/**
 * Eigenvalues of a general square matrix (**complex**), descending.
 *
 * Routes symmetric input through tridiagonal QL and everything else through
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * General eigenvalues into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref eigvals.
 * @param out destination; a contiguous length-n complex vector, overwritten with the descending spectrum.
 * @param a square matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the Hessenberg + shifted-QR iteration allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void eigvals(CNDArray& out, const NDArray& a);
/// @endcond
/**
 * Eigen-decomposition of a symmetric matrix (Householder tridiagonalization + QL).
 *
 * Reduces @p a to tridiagonal form by Householder reflections, then diagonalizes it
 * with implicit-shift QL, returning real eigenvalues sorted descending with matching
 * eigenvector columns; it reads the full matrix and assumes symmetry rather than
 * checking it, so asymmetric input yields meaningless results. Throws on a non-square
 * matrix (or if the QL iteration fails to converge).
 * @param a square symmetric matrix.
 * @return @ref Eig with descending values and matching eigenvectors.
 * @complexity iterative O(n³).
 * @alloc allocates both members.
 * @test LinalgRoutines.SvdAndEigh
 * @crtest LinalgCompileRun.Eigh
 * @systest StdlibE2E.Linalg
 */
Eig eigh(const NDArray& a);                               // symmetric / Hermitian
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Symmetric eigendecomposition into the caller's buffers (outs FIRST) — the buffer-reuse overload
 * of @ref eigh, filling @p values and @p vectors instead of allocating an @ref Eig.
 * @param values destination for the real eigenvalues; a contiguous length-n vector, overwritten.
 * @param vectors destination for the eigenvectors (columns); a contiguous n×n matrix, overwritten.
 * @param a square symmetric matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p values and @p vectors (computed into private scratch, then copied in).
 * @test LinalgRoutines.DecompositionOutReusesBuffer
 */
void eigh(NDArray& values, NDArray& vectors, const NDArray& a);
/// @endcond
/**
 * Eigenvalues of a symmetric matrix, descending (tridiagonal QL).
 *
 * Same tridiagonalization + QL as @ref eigh but **skips the eigenvector accumulation
 * entirely** (the bulk of the work), so it is roughly twice as fast as `eigh`; assumes
 * (does not verify) symmetry and throws on a non-square matrix.
 * @param a square symmetric matrix.
 * @return length-n vector of eigenvalues.
 * @complexity iterative O(n³).
 * @alloc allocates a new NDArray result.
 * @test LinalgRoutines.EigvalshSymmetric
 * @crtest LinalgCompileRun.Eigvalsh
 * @systest StdlibE2E.Linalg
 */
NDArray eigvalsh(const NDArray& a);
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Symmetric eigenvalues into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref eigvalsh.
 * @param out destination; a contiguous length-n vector, overwritten with the descending eigenvalues.
 * @param a square symmetric matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the tridiagonal-QL solver allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void eigvalsh(NDArray& out, const NDArray& a);
/// @endcond
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
 * the real symmetric tridiagonal-QL solver on the 2n×2n real embedding
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Complex Hermitian eigendecomposition into the caller's buffers (outs FIRST) — the buffer-reuse
 * overload of the complex @ref eigh, filling @p values and @p vectors instead of allocating an
 * @ref EighC.
 * @param values destination for the real eigenvalues; a contiguous length-n vector, overwritten.
 * @param vectors destination for the complex eigenvectors (columns); a contiguous n×n matrix, overwritten.
 * @param a square complex Hermitian matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p values and @p vectors (computed into private scratch, then copied in).
 * @test LinalgRoutines.DecompositionOutReusesBuffer
 */
void eigh(NDArray& values, CNDArray& vectors, const CNDArray& a);
/// @endcond
/**
 * Eigenvalues of a **complex Hermitian** matrix, descending and **real**.
 *
 * Same 2n-embedding tridiagonal-QL diagonalization as the complex @ref eigh but discards the
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Complex Hermitian eigenvalues into the caller's buffer @p out (out FIRST) — the buffer-reuse
 * overload of the complex @ref eigvalsh.
 * @param out destination; a contiguous length-n real vector, overwritten with the descending eigenvalues.
 * @param a square complex Hermitian matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the 2n-embedding tridiagonal-QL solver allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void eigvalsh(NDArray& out, const CNDArray& a);
/// @endcond

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
 * @alloc none for a contiguous operand (summed in place); a non-contiguous view packs
 *        once. Returns a double.
 * @test LinalgRoutines.NormAndRank
 * @crtest LinalgCompileRun.Norm
 * @systest StdlibE2E.Linalg
 */
double norm(const NDArray& a);                            // default: Frobenius / L2
/**
 * 2-norm condition number σ_max/σ_min (∞ if singular).
 *
 * Takes the ratio of largest to smallest singular value from a Golub–Reinsch SVD
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
 * @alloc none — reads the diagonal straight from the buffer. Returns a double.
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Solve into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of @ref solve.
 * @param out destination; a contiguous length-n vector, overwritten with the solution x.
 * @param a square coefficient matrix.
 * @param b right-hand-side vector.
 * @complexity O(n³).
 * @alloc reuses @p out; the LU factorization allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void solve(NDArray& out, const NDArray& a, const NDArray& b);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Least-squares solution into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref lstsq. Routes through the @ref matmul out-param so the final product is written into @p out
 * with no allocation.
 * @param out destination; a contiguous array of the solution's shape, overwritten.
 * @param a m×n matrix.
 * @param b right-hand side.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the pseudo-inverse SVD allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void lstsq(NDArray& out, const NDArray& a, const NDArray& b);
/// @endcond
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Inverse into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of @ref inv.
 * @param out destination; a contiguous n×n matrix, overwritten with A⁻¹.
 * @param a square matrix.
 * @complexity O(n³).
 * @alloc reuses @p out; the LU factorization allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void inv(NDArray& out, const NDArray& a);
/// @endcond
/**
 * Moore–Penrose pseudo-inverse via SVD (any shape).
 *
 * Computes V·diag(1/σ)·Uᵀ from a Golub–Reinsch SVD, transposing wide matrices
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
/// @cond INTERNAL — the allocation-free out-parameter variant (see README: buffer reuse)
/**
 * Pseudo-inverse into the caller's buffer @p out (out FIRST) — the buffer-reuse overload of
 * @ref pinv.
 * @param out destination; a contiguous n×m matrix (for an m×n input), overwritten with the pseudo-inverse.
 * @param a m×n matrix.
 * @complexity iterative O(n³).
 * @alloc reuses @p out; the Golub–Reinsch SVD allocates its own scratch.
 * @test LinalgRoutines.FactorizationOutReusesBuffer
 */
void pinv(NDArray& out, const NDArray& a);
/// @endcond

} // namespace cheatah::linalg

// Host (CPU) defaults for the backend-dispatch CPOs — included last, so the concrete
// out-parameter kernels declared above are visible to the delegating tag_invoke overloads.
#include "host_backend.hpp"
