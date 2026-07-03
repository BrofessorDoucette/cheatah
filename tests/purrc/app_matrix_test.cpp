// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level "application" test: a small linear-algebra solver written in
// cheatah that only passes if FOUR stdlib modules cooperate end to end —
//   * ndarray : array / reshape / sub          (data construction + elementwise)
//   * linalg  : solve / matmul / det / trace / norm   (the numerical core)
//   * math    : sqrt                            (scalar check on a linalg result)
//   * io      : print                           (deterministic reporting)
//
// The program solves A x = b for a 3x3 system engineered to have the exact
// integer solution x = [1, 2, 3] (so to_string is byte-stable), then verifies
// the answer independently: it reconstructs A·x via linalg.matmul, forms the
// residual r = A·x − b with ndarray.sub, and confirms ||r|| ≈ 0. det and trace
// are printed as integer-valued cross-checks. The whole pipeline runs as a
// compiled .purr module under the cheatah runtime; this test just compiles it
// with purrc, runs it, and asserts the exact stdout (verified by hand-running
// the program before hard-coding the expectation).

#include "e2e_harness.hpp"

TEST(SystemApps, LinearSolve) {
    e2e::expect_e2e("app_matrix", R"PURR(import io
import ndarray
import linalg
import math

# A small linear-algebra "solver app": solve A x = b for a 3x3 system whose
# exact solution is the integer vector x = [1, 2, 3], then independently verify
# the result via det / trace and a residual norm computed by hand.
let A = ndarray.reshape(ndarray.array([2.0, 1.0, 1.0,  1.0, 3.0, 2.0,  1.0, 0.0, 0.0]), [3, 3])
let b = ndarray.reshape(ndarray.array([7.0, 13.0, 1.0]), [3, 1])

# Solve, then reshape the solution to a column vector for matmul.
let x = linalg.solve(A, b)
let xcol = ndarray.reshape(x, [3, 1])

# Residual r = A x - b; ||r|| should be ~0 if everything cooperated.
let resid = ndarray.sub(linalg.matmul(A, xcol), b)
let rnorm = linalg.norm(resid)

# math.sqrt(rnorm*rnorm) == |rnorm|, exercising math on a linalg scalar.
io.print("solution", ndarray.to_string(x))
io.print("det", linalg.det(A))
io.print("trace", linalg.trace(A))
io.print("residual_ok", math.sqrt(rnorm * rnorm) < 0.000001)
)PURR",
               "solution [1, 2, 3]\n"
               "det -1\n"
               "trace 5\n"
               "residual_ok True\n");
}
