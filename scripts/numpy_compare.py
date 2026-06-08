#!/usr/bin/env python3
"""HONEST cheatah `linalg` vs NumPy comparison.

NumPy's array ops dispatch to BLAS/LAPACK (blocked, vectorized, often multi-threaded
Fortran), so this is the hard comparison — and the honest one. We do NOT tilt it in
cheatah's favor: the SAME matrix (a fixed-seed, well-conditioned one) is fed to both
libraries, each runs the SAME operation N times with the result consumed, and we
print both answers so you can see they agree. Expect:

  * small matrices  → cheatah often WINS (no Python call + NumPy dispatch overhead);
  * large matrices  → NumPy WINS (BLAS is a hand-tuned, threaded kernel — we don't
                      pretend otherwise);
  * the crossover is the interesting part.

The cheatah side compiles a real .purr that loops the op (linalg routines live in a
separate .a, so the optimizer can't hoist or delete the opaque call — verified by an
empty-loop guard); the NumPy side loops the op in CPython. Build `release` first.

    python3 scripts/numpy_compare.py
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
TRIALS = 5
rng = np.random.default_rng(0)


def lit(M):
    """A cheatah list literal of M's row-major elements (full float precision)."""
    return "[" + ", ".join(repr(float(v)) for v in np.asarray(M).flatten()) + "]"


def spd(n):
    """A symmetric positive-definite, well-conditioned n×n matrix (good for solve/inv/
    det/eig). Same matrix used on both sides."""
    A = rng.standard_normal((n, n))
    return A @ A.T + n * np.eye(n)


def run(argv):
    return subprocess.run(argv, capture_output=True, text=True)


def parse(stdout):
    lines = [l for l in stdout.strip().splitlines() if l.strip()]
    if len(lines) < 2:
        return None, None
    try:
        return float(lines[-1]), lines[-2]
    except ValueError:
        return None, None


def time_cheatah(setup, expr, iters, extract="[0]"):
    """setup builds the operands; `expr` is the op; we accumulate a scalar from it
    (the bare scalar when extract=='', else element extract like [0] or [0, 0]) so the
    opaque library call is genuinely consumed and can't be optimized away."""
    consume = expr if extract == "" else f"ndarray.get({expr}, {extract})"
    body = f"acc = acc + {consume}"
    src = (
        "import io\nimport time\nimport ndarray\nimport linalg\n" + setup + "\n"
        "let acc = 0.0\n"
        "let t0 = time.monotonic()\n"
        f"for i in range(0, {iters}) {{\n    {body}\n}}\n"
        "let t1 = time.monotonic()\n"
        "io.print(acc)\nio.print(t1 - t0)\n"
    )
    with tempfile.TemporaryDirectory() as d:
        purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
        open(purr, "w").write(src)
        if run([PURRC, purr, "-o", so]).returncode != 0:
            return None, None
        best, acc = None, None
        for _ in range(TRIALS):
            t, a = parse(run([CHEATAH, so]).stdout)
            if t is not None and (best is None or t < best):
                best, acc = t, a
        return best, acc


def time_numpy(build, op, iters):
    """build returns the operands dict; op(operands, i) -> scalar contribution."""
    import time as _t
    operands = build()
    best, acc = None, None
    for _ in range(TRIALS):
        a = 0.0
        t0 = _t.monotonic()
        for i in range(iters):
            a += op(operands)
        t1 = _t.monotonic()
        if best is None or (t1 - t0) < best:
            best, acc = t1 - t0, a
    return best, repr(acc)


def bench(label, n, iters, ch_setup, ch_expr, np_build, np_op, extract="[0]"):
    ct, c_acc = time_cheatah(ch_setup, ch_expr, iters, extract)
    nt, n_acc = time_numpy(np_build, np_op, iters)
    if ct is None:
        print(f"{label:<22}{n:>5}  (cheatah compile/run failed)")
        return
    cu, nu = ct / iters * 1e6, nt / iters * 1e6  # µs per op
    winner = "cheatah" if ct < nt else "numpy"
    ratio = (nt / ct) if ct < nt else (ct / nt)
    agree = ""
    try:
        if abs(float(c_acc) - float(n_acc)) > 1e-3 * max(1.0, abs(float(n_acc))):
            agree = f"  ⚠ disagree (cheatah {c_acc} vs numpy {n_acc})"
    except ValueError:
        pass
    print(f"{label:<22}{n:>5}{cu:>11.2f}{nu:>11.2f}   {winner:>7} {ratio:>5.1f}×{agree}")


def main():
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("numpy_compare: build the `release` preset first (need purrc + cheatah).")
    print(f"# cheatah linalg vs NumPy {np.__version__} (BLAS/LAPACK) — µs per op, "
          f"same matrix, result consumed\n")
    print(f"{'operation':<22}{'n':>5}{'cheatah':>11}{'numpy':>11}   {'winner':>13}")
    print("-" * 70)

    # ---- matmul A·B (both n×n) ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 5000), (96, 2000)]:
        A, B = spd(n), spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])\n" \
                f"let B = ndarray.reshape(ndarray.array({lit(B)}), [{n}, {n}])"
        bench("matmul", n, iters, setup, "linalg.matmul(A, B)",
              lambda A=A, B=B: (A, B), lambda o: float((o[0] @ o[1])[0, 0]),
              extract="[0, 0]")

    # ---- solve A·x = b ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 4000)]:
        A, b = spd(n), rng.standard_normal(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])\n" \
                f"let b = ndarray.array({lit(b)})"
        bench("solve", n, iters, setup, "linalg.solve(A, b)",
              lambda A=A, b=b: (A, b), lambda o: float(np.linalg.solve(o[0], o[1])[0]))

    # ---- det(A) ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 5000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("det", n, iters, setup, "linalg.det(A)",
              lambda A=A: (A,), lambda o: float(np.linalg.det(o[0])), extract="")

    # ---- inv(A) ----
    for n, iters in [(4, 100000), (16, 30000), (32, 10000), (64, 3000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("inv", n, iters, setup, "linalg.inv(A)",
              lambda A=A: (A,), lambda o: float(np.linalg.inv(o[0])[0, 0]),
              extract="[0, 0]")

    # ---- eigvalsh(A) (symmetric eigenvalues) ----
    for n, iters in [(4, 100000), (16, 20000), (32, 5000), (64, 1000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        # cheatah returns eigenvalues DESCENDING ([0] = largest); NumPy returns them
        # ASCENDING ([-1] = largest) — read the same end so the cross-check agrees.
        bench("eigvalsh", n, iters, setup, "linalg.eigvalsh(A)",
              lambda A=A: (A,), lambda o: float(np.linalg.eigvalsh(o[0])[-1]))

    # ---- dot (vectors) ----
    for n, iters in [(64, 200000), (1024, 50000), (16384, 5000)]:
        u, v = rng.standard_normal(n), rng.standard_normal(n)
        setup = f"let u = ndarray.array({lit(u)})\nlet v = ndarray.array({lit(v)})"
        bench("dot", n, iters, setup, "linalg.dot(u, v)",
              lambda u=u, v=v: (u, v), lambda o: float(o[0] @ o[1]), extract="")

    print("\nNote: NumPy calls BLAS/LAPACK (often threaded). cheatah's kernels are "
          "single-threaded\nauto-vectorized C++. Small n favors cheatah (no Python/"
          "dispatch overhead); large n favors BLAS.")


if __name__ == "__main__":
    main()
