// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level end-to-end test for the cheatah `linalg` stdlib module.
//
// Unlike the per-function compile-run tests (linalg_cr_test.cpp), this is ONE
// cohesive linear-algebra program that exercises EVERY purr-callable routine in
// stdlib/linalg/routines.hpp in a single run: the 7 matrix/vector products
// (dot, vdot, inner, outer, matmul, matrix_power, kron), the 3 decompositions
// (cholesky, qr, svd), the 4 eigen routines (eig, eigvals, eigh, eigvalsh), the
// 6 "numbers" routines (norm, cond, det, matrix_rank, slogdet, trace), and the
// 4 solve/invert routines (solve, lstsq, inv, pinv) — 24 functions total.
//
// It builds a few fixed operands (two vectors, an SPD diagonal matrix, a general
// 2x2, and the 2x2 identity) and runs a sequence of operations over them. All
// printed values are integer-valued or exactly representable so io.print's
// formatting is deterministic. Struct-returning routines print a derived single
// field (qr().r, svd().s, eig().values, eigh().values, slogdet().sign).
#include "e2e_harness.hpp"

TEST(StdlibE2E, Linalg) {
    e2e::expect_e2e("linalg_sys", R"PURR(import io
import ndarray
import linalg

# Fixed building blocks: a couple of small vectors and matrices.
let u = ndarray.array([1.0, 2.0, 3.0])
let v = ndarray.array([4.0, 5.0, 6.0])

# A symmetric positive-definite diagonal matrix and a general 2x2.
let d = ndarray.reshape(ndarray.array([4.0, 0.0, 0.0, 9.0]), [2, 2])
let g = ndarray.reshape(ndarray.array([4.0, 3.0, 6.0, 3.0]), [2, 2])
let id2 = ndarray.reshape(ndarray.array([1.0, 0.0, 0.0, 1.0]), [2, 2])

# ---- products ----
io.print(linalg.dot(u, v))
io.print(linalg.vdot(u, v))
io.print(linalg.inner(u, v))
io.print(ndarray.to_string(linalg.outer(ndarray.array([1.0, 2.0]), ndarray.array([3.0, 4.0]))))
io.print(ndarray.to_string(linalg.matmul(d, id2)))
io.print(ndarray.to_string(linalg.matrix_power(d, 2)))
io.print(ndarray.to_string(linalg.kron(id2, ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2]))))

# ---- decompositions (print derived scalars / single fields) ----
io.print(ndarray.to_string(linalg.cholesky(d)))
io.print(ndarray.to_string(linalg.qr(d).r))
io.print(ndarray.to_string(linalg.svd(d).s))

# ---- eigenvalues ----
io.print(ndarray.to_string(linalg.eig(d).values))
io.print(ndarray.to_string(linalg.eigvals(d)))
io.print(ndarray.to_string(linalg.eigh(d).values))
io.print(ndarray.to_string(linalg.eigvalsh(d)))

# ---- norms and numbers ----
io.print(linalg.norm(ndarray.array([3.0, 4.0])))
io.print(linalg.cond(d))
io.print(linalg.det(g))
io.print(linalg.matrix_rank(d))
io.print(linalg.slogdet(d).sign)
io.print(linalg.trace(g))

# ---- solving and inverting ----
io.print(ndarray.to_string(linalg.solve(g, ndarray.array([10.0, 12.0]))))
io.print(ndarray.to_string(linalg.lstsq(id2, ndarray.reshape(ndarray.array([5.0, 7.0]), [2, 1]))))
io.print(ndarray.to_string(linalg.inv(d)))
io.print(ndarray.to_string(linalg.pinv(d)))
)PURR",
                    "32\n"
                    "32\n"
                    "32\n"
                    "[[3, 4], [6, 8]]\n"
                    "[[4, 0], [0, 9]]\n"
                    "[[16, 0], [0, 81]]\n"
                    "[[1, 2, 0, 0], [3, 4, 0, 0], [0, 0, 1, 2], [0, 0, 3, 4]]\n"
                    "[[2, 0], [0, 3]]\n"
                    "[[-4, 0], [0, -9]]\n"
                    "[9, 4]\n"          // svd().s
                    "[9+0j, 4+0j]\n"    // eig().values   — general -> complex spectrum
                    "[9+0j, 4+0j]\n"    // eigvals()       — general -> complex spectrum
                    "[9, 4]\n"          // eigh().values  — Hermitian -> real
                    "[9, 4]\n"          // eigvalsh()      — Hermitian -> real
                    "5\n"
                    "2.25\n"
                    "-6\n"
                    "2\n"
                    "1\n"
                    "7\n"
                    "[1, 2]\n"
                    "[[5], [7]]\n"
                    "[[0.25, 0], [0, 0.111111]]\n"
                    "[[0.25, 0], [0, 0.111111]]\n");
}

// Complex linear algebra exercised together end-to-end: build complex vectors and a
// matrix, take bilinear (dot) and Hermitian (vdot) inner products, the conjugate
// transpose, and a complex matmul. M·Mᴴ is Hermitian; vdot(a,a) is the real ‖a‖².
TEST(StdlibE2E, LinalgComplex) {
    e2e::expect_e2e("linalg_complex_sys", R"PURR(import io
import ndarray
import linalg

let a = ndarray.complex(ndarray.array([1.0, 3.0]), ndarray.array([2.0, -1.0]))
let b = ndarray.complex(ndarray.array([0.0, 2.0]), ndarray.array([1.0, 0.0]))
let M = ndarray.reshape(ndarray.complex(ndarray.array([1.0, 2.0, 0.0, 3.0]), ndarray.array([1.0, 0.0, 0.0, -1.0])), [2, 2])
let H = ndarray.reshape(ndarray.complex(ndarray.array([2.0, 1.0, 1.0, 3.0]), ndarray.array([0.0, 1.0, -1.0, 0.0])), [2, 2])

io.print(linalg.dot(a, b))
io.print(linalg.vdot(a, b))
io.print(linalg.vdot(a, a))
io.print(ndarray.to_string(linalg.conj_transpose(M)))
io.print(ndarray.to_string(linalg.matmul(M, linalg.conj_transpose(M))))
io.print(ndarray.to_string(linalg.eigvalsh(H)))
io.print(ndarray.to_string(linalg.eigh(H).values))
)PURR",
                    "4-1j\n"
                    "8+3j\n"
                    "15+0j\n"
                    "[[1-1j, 0+0j], [2+0j, 3+1j]]\n"
                    "[[6+0j, 6+2j], [6-2j, 10+0j]]\n"
                    "[4, 1]\n"
                    "[4, 1]\n");
}
