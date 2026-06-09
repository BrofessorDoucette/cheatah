// Compile-run unit tests for the `linalg` module: one test per purr-callable
// function. Each writes a tiny .purr that calls a single linalg routine on a
// small fixed matrix/vector, compiles it with purrc, runs it under the cheatah
// runtime, and asserts the exact stdout. Results are chosen to be integer-valued
// (or exactly representable) so the io.print formatting is deterministic.
// Complements the in-process unit tests (stdlib/tests/linalg_routines_test.cpp),
// the per-module system test (StdlibE2E.Linalg), and the least-squares system
// test (linalg_lsq_e2e_test.cpp).
#include "e2e_harness.hpp"

// ---- Matrix and vector products ----

TEST(LinalgCompileRun, Dot) {
    e2e::expect_e2e("linalg_dot", R"PURR(import io
import ndarray
import linalg
io.print(linalg.dot(ndarray.array([1.0, 2.0, 3.0]), ndarray.array([4.0, 5.0, 6.0])))
)PURR", "32\n");
}

TEST(LinalgCompileRun, Vdot) {
    e2e::expect_e2e("linalg_vdot", R"PURR(import io
import ndarray
import linalg
io.print(linalg.vdot(ndarray.array([1.0, 2.0, 3.0]), ndarray.array([4.0, 5.0, 6.0])))
)PURR", "32\n");
}

TEST(LinalgCompileRun, Inner) {
    e2e::expect_e2e("linalg_inner", R"PURR(import io
import ndarray
import linalg
io.print(linalg.inner(ndarray.array([1.0, 2.0, 3.0]), ndarray.array([4.0, 5.0, 6.0])))
)PURR", "32\n");
}

TEST(LinalgCompileRun, Outer) {
    e2e::expect_e2e("linalg_outer", R"PURR(import io
import ndarray
import linalg
io.print(ndarray.to_string(linalg.outer(ndarray.array([1.0, 2.0]), ndarray.array([3.0, 4.0]))))
)PURR", "[[3, 4], [6, 8]]\n");
}

TEST(LinalgCompileRun, Matmul) {
    e2e::expect_e2e("linalg_matmul", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
let b = ndarray.reshape(ndarray.array([5.0, 6.0, 7.0, 8.0]), [2, 2])
io.print(ndarray.to_string(linalg.matmul(a, b)))
)PURR", "[[19, 22], [43, 50]]\n");
}

TEST(LinalgCompileRun, MatrixPower) {
    e2e::expect_e2e("linalg_matrix_power", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 3.0]), [2, 2])
io.print(ndarray.to_string(linalg.matrix_power(a, 3)))
)PURR", "[[8, 0], [0, 27]]\n");
}

TEST(LinalgCompileRun, Kron) {
    e2e::expect_e2e("linalg_kron", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 0.0, 0.0, 1.0]), [2, 2])
let b = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
io.print(ndarray.to_string(linalg.kron(a, b)))
)PURR", "[[1, 2, 0, 0], [3, 4, 0, 0], [0, 0, 1, 2], [0, 0, 3, 4]]\n");
}

// ---- Decompositions ----

TEST(LinalgCompileRun, Cholesky) {
    e2e::expect_e2e("linalg_cholesky", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([4.0, 0.0, 0.0, 9.0]), [2, 2])
io.print(ndarray.to_string(linalg.cholesky(a)))
)PURR", "[[2, 0], [0, 3]]\n");
}

TEST(LinalgCompileRun, Qr) {
    // Diagonal input -> Householder gives diagonal R with the reflector's sign
    // convention (negated diagonal), which is exact and deterministic.
    e2e::expect_e2e("linalg_qr", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([6.0, 0.0, 0.0, 5.0]), [2, 2])
let f = linalg.qr(a)
io.print(ndarray.to_string(f.r))
)PURR", "[[-6, 0], [0, -5]]\n");
}

TEST(LinalgCompileRun, Svd) {
    e2e::expect_e2e("linalg_svd", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([4.0, 0.0, 0.0, 9.0]), [2, 2])
let f = linalg.svd(a)
io.print(ndarray.to_string(f.s))
)PURR", "[9, 4]\n");
}

TEST(LinalgCompileRun, Svdvals) {
    e2e::expect_e2e("linalg_svdvals", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([4.0, 0.0, 0.0, 9.0]), [2, 2])
io.print(ndarray.to_string(linalg.svdvals(a)))
)PURR", "[9, 4]\n");
}

// ---- Matrix eigenvalues ----

TEST(LinalgCompileRun, Eig) {
    e2e::expect_e2e("linalg_eig", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 3.0]), [2, 2])
let e = linalg.eig(a)
io.print(ndarray.to_string(e.values))
)PURR", "[3+0j, 2+0j]\n");  // general eig -> complex spectrum (real parts here)
}

TEST(LinalgCompileRun, Eigvals) {
    e2e::expect_e2e("linalg_eigvals", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 3.0]), [2, 2])
io.print(ndarray.to_string(linalg.eigvals(a)))
)PURR", "[3+0j, 2+0j]\n");
}

TEST(LinalgCompileRun, EigvalsComplex) {
    // A real rotation matrix [[0,-1],[1,0]] has eigenvalues ±i — printed Python-style.
    e2e::expect_e2e("linalg_eigvals_complex", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([0.0, -1.0, 1.0, 0.0]), [2, 2])
io.print(ndarray.to_string(linalg.eigvals(a)))
)PURR", "[0+1j, 0-1j]\n");
}

TEST(LinalgCompileRun, Eigh) {
    e2e::expect_e2e("linalg_eigh", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 5.0]), [2, 2])
let e = linalg.eigh(a)
io.print(ndarray.to_string(e.values))
)PURR", "[5, 2]\n");
}

TEST(LinalgCompileRun, Eigvalsh) {
    e2e::expect_e2e("linalg_eigvalsh", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 5.0]), [2, 2])
io.print(ndarray.to_string(linalg.eigvalsh(a)))
)PURR", "[5, 2]\n");
}

// ---- Norms and other numbers ----

TEST(LinalgCompileRun, Norm) {
    e2e::expect_e2e("linalg_norm", R"PURR(import io
import ndarray
import linalg
io.print(linalg.norm(ndarray.array([3.0, 4.0])))
)PURR", "5\n");
}

TEST(LinalgCompileRun, Cond) {
    e2e::expect_e2e("linalg_cond", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 8.0]), [2, 2])
io.print(linalg.cond(a))
)PURR", "4\n");
}

TEST(LinalgCompileRun, Det) {
    e2e::expect_e2e("linalg_det", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
io.print(linalg.det(a))
)PURR", "-2\n");
}

TEST(LinalgCompileRun, MatrixRank) {
    // Rank-1 matrix (second row = 2x first) -> numerical rank 1.
    e2e::expect_e2e("linalg_matrix_rank", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 2.0, 2.0, 4.0]), [2, 2])
io.print(linalg.matrix_rank(a))
)PURR", "1\n");
}

TEST(LinalgCompileRun, Slogdet) {
    // det = 6 > 0, so the sign is exactly +1 (avoids the float logabsdet).
    e2e::expect_e2e("linalg_slogdet", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 3.0]), [2, 2])
let r = linalg.slogdet(a)
io.print(r.sign)
)PURR", "1\n");
}

TEST(LinalgCompileRun, Trace) {
    e2e::expect_e2e("linalg_trace", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
io.print(linalg.trace(a))
)PURR", "5\n");
}

// ---- Solving equations and inverting matrices ----

TEST(LinalgCompileRun, Solve) {
    // 4x+3y=10, 6x+3y=12 -> x=1, y=2.
    e2e::expect_e2e("linalg_solve", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([4.0, 3.0, 6.0, 3.0]), [2, 2])
io.print(ndarray.to_string(linalg.solve(a, ndarray.array([10.0, 12.0]))))
)PURR", "[1, 2]\n");
}

TEST(LinalgCompileRun, Lstsq) {
    // Identity system with a column-vector RHS -> x = b exactly.
    e2e::expect_e2e("linalg_lstsq", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([1.0, 0.0, 0.0, 1.0]), [2, 2])
let b = ndarray.reshape(ndarray.array([5.0, 7.0]), [2, 1])
io.print(ndarray.to_string(linalg.lstsq(a, b)))
)PURR", "[[5], [7]]\n");
}

TEST(LinalgCompileRun, Inv) {
    e2e::expect_e2e("linalg_inv", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 4.0]), [2, 2])
io.print(ndarray.to_string(linalg.inv(a)))
)PURR", "[[0.5, 0], [0, 0.25]]\n");
}

TEST(LinalgCompileRun, Pinv) {
    e2e::expect_e2e("linalg_pinv", R"PURR(import io
import ndarray
import linalg
let a = ndarray.reshape(ndarray.array([2.0, 0.0, 0.0, 4.0]), [2, 2])
io.print(ndarray.to_string(linalg.pinv(a)))
)PURR", "[[0.5, 0], [0, 0.25]]\n");
}

// ---- complex products (complex inner-product spaces) ----

TEST(LinalgCompileRun, ComplexDot) {
    e2e::expect_e2e("linalg_complex_dot", R"PURR(import io
import ndarray
import linalg
let a = ndarray.complex(ndarray.array([1.0, 3.0]), ndarray.array([2.0, -1.0]))
let b = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, 0.0]))
io.print(linalg.dot(a, b))
)PURR", "4-1j\n");
}

TEST(LinalgCompileRun, ComplexVdot) {
    e2e::expect_e2e("linalg_complex_vdot", R"PURR(import io
import ndarray
import linalg
let a = ndarray.complex(ndarray.array([1.0, 3.0]), ndarray.array([2.0, -1.0]))
let b = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, 0.0]))
io.print(linalg.vdot(a, b))
)PURR", "8+3j\n");
}

TEST(LinalgCompileRun, ComplexMatmul) {
    e2e::expect_e2e("linalg_complex_matmul", R"PURR(import io
import ndarray
import linalg
let M = ndarray.reshape(ndarray.complex(ndarray.array([1.0, 0.0, 0.0, 1.0]), ndarray.array([1.0, 0.0, 0.0, 1.0])), [2, 2])
let I = ndarray.reshape(ndarray.complex(ndarray.array([1.0, 0.0, 0.0, 1.0]), ndarray.array([0.0, 0.0, 0.0, 0.0])), [2, 2])
io.print(ndarray.to_string(linalg.matmul(M, I)))
)PURR", "[[1+1j, 0+0j], [0+0j, 1+1j]]\n");
}

TEST(LinalgCompileRun, ConjTranspose) {
    e2e::expect_e2e("linalg_conj_transpose", R"PURR(import io
import ndarray
import linalg
let M = ndarray.reshape(ndarray.complex(ndarray.array([1.0, 2.0, 0.0, 3.0]), ndarray.array([1.0, 0.0, 0.0, -1.0])), [2, 2])
io.print(ndarray.to_string(linalg.conj_transpose(M)))
)PURR", "[[1-1j, 0+0j], [2+0j, 3+1j]]\n");
}

TEST(LinalgCompileRun, EighComplex) {
    // Hermitian [[2, 1+i],[1-i, 3]] -> real eigenvalues 4, 1.
    e2e::expect_e2e("linalg_eigh_complex", R"PURR(import io
import ndarray
import linalg
let H = ndarray.reshape(ndarray.complex(ndarray.array([2.0, 1.0, 1.0, 3.0]), ndarray.array([0.0, 1.0, -1.0, 0.0])), [2, 2])
io.print(ndarray.to_string(linalg.eigh(H).values))
)PURR", "[4, 1]\n");
}

TEST(LinalgCompileRun, EigvalshComplex) {
    e2e::expect_e2e("linalg_eigvalsh_complex", R"PURR(import io
import ndarray
import linalg
let H = ndarray.reshape(ndarray.complex(ndarray.array([2.0, 1.0, 1.0, 3.0]), ndarray.array([0.0, 1.0, -1.0, 0.0])), [2, 2])
io.print(ndarray.to_string(linalg.eigvalsh(H)))
)PURR", "[4, 1]\n");
}
