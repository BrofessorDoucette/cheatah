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
import atexit
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
# 7 striated rounds replacing 5 best-of-N trials per side. See scripts/perf_suite.py for
# the reasoning: a minimum has no dispersion, and taking each side's minimum independently
# pairs one side's luckiest run against the other's.
ROUNDS = 7
rng = np.random.default_rng(0)


def lit(M):
    """A cheatah list literal of M's row-major elements (full float precision)."""
    return "[" + ", ".join(repr(float(v)) for v in np.asarray(M).flatten()) + "]"


def spd(n):
    """A symmetric positive-definite, well-conditioned n×n matrix (good for solve/inv/
    det/eig). Same matrix used on both sides."""
    A = rng.standard_normal((n, n))
    return A @ A.T + n * np.eye(n)


def gen(n):
    """A general (non-symmetric) well-conditioned n×n matrix — for the general
    eigenvalues and matrix_power (which must not route through the symmetric path)."""
    return rng.standard_normal((n, n)) * 0.3 + np.eye(n)


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


_TEMPDIRS = []
atexit.register(lambda: [shutil.rmtree(d, ignore_errors=True) for d in _TEMPDIRS])


def build_cheatah(setup, expr, iters, extract="[0]"):
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
    # mkdtemp, not TemporaryDirectory: the .so must outlive this call so the rounds below can
    # re-run it without recompiling. Registered for cleanup at exit rather than leaked.
    d = tempfile.mkdtemp(prefix="numpy_compare.")
    _TEMPDIRS.append(d)
    purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
    open(purr, "w").write(src)
    if run([PURRC, purr, "-o", so]).returncode != 0:
        return None
    return so


def _median(xs):
    ys = sorted(xs)
    n = len(ys)
    return ys[n // 2] if n % 2 else 0.5 * (ys[n // 2 - 1] + ys[n // 2])


def np_once(operands, op, iters):
    """One timed NumPy pass. op(operands, i) -> scalar contribution; `i` lets a case vary
    its input per iteration (to match a cheatah loop that does the same)."""
    import time as _t
    a = 0.0
    t0 = _t.monotonic()
    for i in range(iters):
        a += op(operands, i)
    return _t.monotonic() - t0, repr(a)


ROWS = []   # (label, n, cheatah_us, numpy_us, ratio, lo, hi) for the generated table


def bench(label, n, iters, ch_setup, ch_expr, np_build, np_op, extract="[0]"):
    # Compile ONCE, outside the rounds: compilation is not the thing being measured, and
    # re-running purrc between rounds would put seconds between the two sides of a pair.
    so = build_cheatah(ch_setup, ch_expr, iters, extract)
    if so is None:
        print(f"{label:<22}{n:>5}  (cheatah compile/run failed)")
        return
    operands = np_build()

    # ROUNDS striated rounds: cheatah then NumPy, adjacently, before either repeats. The
    # headline is the median of the per-round PAIRED ratios — not the ratio of two medians,
    # which the two only agree on when the machine holds perfectly still.
    ch_ts, np_ts, ratios = [], [], []
    c_acc = n_acc = None
    for _ in range(ROUNDS):
        ct, ca = parse(run([CHEATAH, so]).stdout)
        if ct is None:
            print(f"{label:<22}{n:>5}  (cheatah run failed)")
            return
        nt, na = np_once(operands, np_op, iters)
        ch_ts.append(ct)
        np_ts.append(nt)
        ratios.append(nt / ct)
        c_acc, n_acc = ca, na

    ct, nt = _median(ch_ts), _median(np_ts)
    cu, nu = ct / iters * 1e6, nt / iters * 1e6            # µs per op
    r = _median(ratios)                                     # >1 = cheatah faster
    winner = "cheatah" if r > 1.0 else "numpy"
    ratio = r if r > 1.0 else 1.0 / r
    lo, hi = min(ratios), max(ratios)
    agree = ""
    try:
        if abs(float(c_acc) - float(n_acc)) > 1e-3 * max(1.0, abs(float(n_acc))):
            agree = f"  ⚠ disagree (cheatah {c_acc} vs numpy {n_acc})"
    except ValueError:
        pass
    # The raw band is numpy/cheatah in both directions, so a reader can see the swing without
    # it being folded through the winner flip.
    band = f" [{lo:.2f}–{hi:.2f} raw]"
    ROWS.append((label, n, cu, nu, r, lo, hi))
    print(f"{label:<22}{n:>5}{cu:>11.2f}{nu:>11.2f}   {winner:>7} {ratio:>5.1f}×{band}{agree}")



def _capture(cmd):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout.strip()
    except Exception:
        return ""


def _blas():
    """WHICH BLAS, and how many threads. Where the crossovers land depends far more on the
    linked BLAS (reference vs OpenBLAS vs MKL) and its thread count than on the NumPy version,
    so a stamp naming only the version cannot be reproduced from.

    np.show_config() is consulted first but on a system BLAS it honestly answers
    "blas / unknown", which identifies nothing. In that case resolve the shared object NumPy
    actually links, which does."""
    name = ""
    try:
        blas = np.show_config(mode="dicts").get("Build Dependencies", {}).get("blas", {})
        n, v = blas.get("name", ""), blas.get("version", "")
        if n and n != "blas" and v and v != "unknown":
            name = f"{n} {v}"
    except Exception:
        pass
    if not name:
        # ldd the extension module that carries the BLAS dependency. The subdirectory moved
        # between NumPy versions (core/ -> _core/), so glob rather than hardcode it.
        cands = glob.glob(os.path.join(os.path.dirname(np.__file__), "**",
                                       "_multiarray_umath*.so"), recursive=True)
        if cands:
            libs = _capture(f"ldd {cands[0]} 2>/dev/null | grep -iE 'blas|mkl' | head -2")
            resolved = []
            for line in libs.splitlines():
                soname = line.split("=>")[0].strip()
                path = line.split("=>")[1].split("(")[0].strip() if "=>" in line else ""
                # On Debian libblas.so.3 is an alternatives symlink; the target is the thing
                # that actually determines the numbers, so report what it points at.
                real = _capture(f"readlink -f {path}") if path else ""
                resolved.append(f"{soname} -> {os.path.basename(real)}" if real else soname)
            if resolved:
                name = "; ".join(resolved)
    if not name:
        name = "unidentified system BLAS"
    threads = (os.environ.get("OPENBLAS_NUM_THREADS") or os.environ.get("OMP_NUM_THREADS")
               or "unset (BLAS default)")
    return f"{name}, threads={threads}"


def write_md(path, suite_name):
    """Emit the generated region body scripts/bench_table.purr expects.

    ONE HARNESS, ONE TABLE. The linalg README used to place a cheatah column measured here
    beside an Eigen column measured by Google Benchmark, with a prose warning not to read
    across. A warning is a worse fix than a structure: this table carries only the columns
    this harness measured, and the Eigen comparison is its own generated table from its own
    harness (docs/bench/linalg-vs-eigen.md)."""
    watch = ("stdlib/linalg/, stdlib/ndarray/, scripts/numpy_compare.py"
             if suite_name.startswith("linalg")
             else "stdlib/ndarray/, scripts/numpy_compare.py")
    commit = _capture("git rev-parse --short HEAD") or "unknown"
    # Judged against THIS suite's watched sources only, with Markdown excluded — see the long
    # note in scripts/bench_run.sh. A whole-tree check means any edit anywhere invalidates
    # every later measurement, which turns one prose fix into a full re-measure.
    subprocess.run("git update-index --refresh", shell=True, capture_output=True)
    spec = " ".join(f"'{p.strip()}'" for p in watch.split(",") if p.strip())
    if spec and subprocess.run(f"git diff --quiet -- {spec} ':!*.md' ':!docs/bench'",
                               shell=True).returncode != 0:
        commit += " (dirty)"
    host = _capture("awk -F': ' '/^model name/{print $2; exit}' /proc/cpuinfo") or platform.machine()
    gov = _capture("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    if gov:
        host += f" (governor={gov})"
    lines = [
        "<!-- cheatah-bench-stamp v1",
        f"     suite:        {suite_name}",
        f"     generated:    {time.strftime('%Y-%m-%d')}",
        f"     commit:       {commit}",
        f"     host:         {host}, {os.cpu_count()} CPUs",
        "     cpu-scaling:  enabled",
        "     build:        purrc -> -O3 -march=native",
        f"     competitors:  NumPy {np.__version__} on {_blas()}, CPython "
        f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        f"     harness:      rounds={ROUNDS}, striated (cheatah and NumPy adjacent in each round)",
        "     statistic:    median of per-round PAIRED ratios; [lo-hi] is the range of those",
        f"     watch:        {watch}",
        "     publishable:  true",
        "",
        "     PRODUCED BY:",
        f"       python3 scripts/numpy_compare.py --suite "
        f"{suite_name.replace('-vs-numpy', '')} --md {path}",
        "-->",
        "",
        "| op | operand dimensions | cheatah | NumPy | winner | band |",
        "|----|--------------------|--------:|------:|--------|------|",
    ]
    for label, n, cu, nu, r, lo, hi in ROWS:
        win = f"**cheatah {r:.1f}x**" if r > 1.0 else f"NumPy {1 / r:.1f}x"
        lines.append(f"| `{label}` | {n} | {cu:.2f} | {nu:.2f} | {win} | {lo:.2f}-{hi:.2f} |")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nwrote {path}")


def main():
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("numpy_compare: build the `release` preset first (need purrc + cheatah).")
    suite = "linalg"
    if "--suite" in sys.argv:
        i = sys.argv.index("--suite")
        if i + 1 >= len(sys.argv):
            sys.exit("numpy_compare: --suite needs a name (linalg|ndarray)")
        suite = sys.argv[i + 1]
        if suite not in ("linalg", "ndarray"):
            sys.exit(f"numpy_compare: unknown suite {suite!r} — expected linalg or ndarray")
    md_out = None
    if "--md" in sys.argv:
        i = sys.argv.index("--md")
        if i + 1 >= len(sys.argv):
            sys.exit("numpy_compare: --md needs an output path")
        md_out = sys.argv[i + 1]
    print(f"# cheatah linalg vs NumPy {np.__version__} (BLAS/LAPACK) — µs per op, "
          f"same matrix, result consumed\n")
    print(f"{'operation':<22}{'n':>5}{'cheatah':>11}{'numpy':>11}   {'winner':>13}")
    print("-" * 70)

    if suite == "ndarray":
        run_ndarray()
    else:
        run_linalg()

    print("\nNote: NumPy calls BLAS/LAPACK (often threaded). cheatah's kernels are "
          "single-threaded\nauto-vectorized C++. Small n favors cheatah (no Python/"
          "dispatch overhead); large n favors BLAS / vectorized ufuncs.")

    if md_out is not None:
        write_md(md_out, f"{suite}-vs-numpy")


def run_linalg():
    # ---- matmul A·B (both n×n) ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 5000), (96, 2000)]:
        A, B = spd(n), spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])\n" \
                f"let B = ndarray.reshape(ndarray.array({lit(B)}), [{n}, {n}])"
        bench("matmul", n, iters, setup, "linalg.matmul(A, B)",
              lambda A=A, B=B: (A, B), lambda o, i: float((o[0] @ o[1])[0, 0]),
              extract="[0, 0]")

    # ---- solve A·x = b ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 4000)]:
        A, b = spd(n), rng.standard_normal(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])\n" \
                f"let b = ndarray.array({lit(b)})"
        bench("solve", n, iters, setup, "linalg.solve(A, b)",
              lambda A=A, b=b: (A, b), lambda o, i: float(np.linalg.solve(o[0], o[1])[0]))

    # ---- det(A) ----
    for n, iters in [(4, 200000), (16, 50000), (32, 20000), (64, 5000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("det", n, iters, setup, "linalg.det(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.det(o[0])), extract="")

    # ---- inv(A) ----
    for n, iters in [(4, 100000), (16, 30000), (32, 10000), (64, 3000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("inv", n, iters, setup, "linalg.inv(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.inv(o[0])[0, 0]),
              extract="[0, 0]")

    # ---- eigvalsh(A) (symmetric eigenvalues) ----
    # Small n is the physicist's common case (few-level systems, spin Hamiltonians,
    # parameter sweeps that solve the same-shape problem millions of times) — measure
    # finely there to find where cheatah's no-overhead Jacobi beats LAPACK dispatch.
    for n, iters in [(2, 200000), (3, 200000), (4, 100000), (6, 80000), (8, 60000),
                     (16, 20000), (32, 5000), (64, 1000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        # cheatah returns eigenvalues DESCENDING ([0] = largest); NumPy returns them
        # ASCENDING ([-1] = largest) — read the same end so the cross-check agrees.
        bench("eigvalsh", n, iters, setup, "linalg.eigvalsh(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.eigvalsh(o[0])[-1]))

    # ---- dot (vectors) ----
    for n, iters in [(64, 200000), (1024, 50000), (16384, 5000)]:
        u, v = rng.standard_normal(n), rng.standard_normal(n)
        setup = f"let u = ndarray.array({lit(u)})\nlet v = ndarray.array({lit(v)})"
        bench("dot", n, iters, setup, "linalg.dot(u, v)",
              lambda u=u, v=v: (u, v), lambda o, i: float(o[0] @ o[1]), extract="")

    # ---- element-wise math ufuncs (cheatah ndarray.sqrt etc. vs numpy np.sqrt) ----
    # cheatah's ndarray ufuncs are header templates (inlined), so a loop-invariant
    # input would be hoisted out; we add a tiny i-dependent scalar each iteration to
    # force the work to run. NumPy does the same add+ufunc (Python never hoists), so
    # the comparison stays fair — both compute "add a scalar, then the ufunc, over n".
    for fn in ["sqrt", "exp", "sin"]:
        npfn = getattr(np, fn)
        for n, iters in [(64, 200000), (1024, 50000), (16384, 4000)]:
            v = rng.random(n) + 0.1  # positive (valid for sqrt)
            setup = f"let v = ndarray.array({lit(v)})"
            expr = f"ndarray.{fn}(ndarray.add(v, ndarray.scalar(0.000001 * i)))"
            bench(f"ndarray.{fn}", n, iters, setup, expr,
                  lambda v=v: (v,),
                  lambda o, i, f=npfn: float(f(o[0] + 0.000001 * i)[0]), extract="[0]")

    # ---- element-wise add of a broadcast scalar — a memory-bandwidth-bound op (one read,
    #      one write). This is where allocating the result buffer UNINITIALIZED instead of
    #      zero-filling it before the overwrite (see ndarray::buffer_t) matters most. ----
    for n, iters in [(64, 200000), (16384, 4000)]:
        v = rng.random(n) + 0.1
        setup = f"let v = ndarray.array({lit(v)})"
        expr = "ndarray.add(v, ndarray.scalar(0.000001 * i))"
        bench("ndarray.add", n, iters, setup, expr,
              lambda v=v: (v,),
              lambda o, i: float((o[0] + 0.000001 * i)[0]), extract="[0]")

    # ---- Cholesky factor (SPD) ----
    for n, iters in [(8, 50000), (32, 8000), (64, 2000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("cholesky", n, iters, setup, "linalg.cholesky(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.cholesky(o[0])[0, 0]),
              extract="[0, 0]")

    # ---- QR (R[0,0]) ----
    for n, iters in [(8, 40000), (32, 6000), (64, 1500)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("qr", n, iters, setup, "linalg.qr(A).r",
              lambda A=A: (A,), lambda o, i: float(np.linalg.qr(o[0])[1][0, 0]),
              extract="[0, 0]")

    # ---- SVD — compared FAIRLY on both sides ----
    # values-only: cheatah `svdvals` vs numpy `svd(compute_uv=False)`
    for n, iters in [(8, 20000), (32, 3000), (64, 800)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("svdvals", n, iters, setup, "linalg.svdvals(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.svd(o[0], compute_uv=False)[0]),
              extract="[0]")
    # full decomposition: cheatah `svd` vs numpy `svd` (both compute U and Vᵀ)
    for n, iters in [(8, 20000), (32, 2000), (64, 500)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("svd (full)", n, iters, setup, "linalg.svd(A).s",
              lambda A=A: (A,), lambda o, i: float(np.linalg.svd(o[0])[1][0]), extract="[0]")

    # ---- pseudo-inverse (SVD-based) ----
    for n, iters in [(8, 20000), (32, 3000), (64, 800)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("pinv", n, iters, setup, "linalg.pinv(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.pinv(o[0])[0, 0]),
              extract="[0, 0]")

    # ---- condition number / matrix_rank (SVD-based) ----
    for n, iters in [(8, 20000), (32, 3000), (64, 800)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("cond", n, iters, setup, "linalg.cond(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.cond(o[0])), extract="")
    for n, iters in [(8, 20000), (32, 3000), (64, 800)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("matrix_rank", n, iters, setup, "linalg.matrix_rank(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.matrix_rank(o[0])), extract="")

    # ---- slogdet (log|det|, LU-based) ----
    for n, iters in [(8, 80000), (32, 15000), (64, 4000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("slogdet", n, iters, setup, "linalg.slogdet(A).logabsdet",
              lambda A=A: (A,), lambda o, i: float(np.linalg.slogdet(o[0])[1]), extract="")

    # ---- eigh (symmetric, eigenvalues AND eigenvectors) ----
    for n, iters in [(8, 40000), (32, 4000), (64, 1000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("eigh", n, iters, setup, "linalg.eigh(A).values",
              lambda A=A: (A,), lambda o, i: float(np.linalg.eigh(o[0])[0][-1]), extract="[0]")

    # ---- eigvals (GENERAL, non-symmetric -> Hessenberg + shifted QR) ----
    for n, iters in [(8, 8000), (16, 2000), (32, 400)]:
        G = gen(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(G)}), [{n}, {n}])"
        bench("eigvals", n, iters, setup, "ndarray.real(linalg.eigvals(A))",
              lambda G=G: (G,), lambda o, i: float(np.sort(np.linalg.eigvals(o[0]).real)[-1]),
              extract="[0]")

    # ---- matrix_power A³ (general) ----
    for n, iters in [(8, 40000), (32, 5000), (64, 1500)]:
        G = gen(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(G)}), [{n}, {n}])"
        bench("matrix_power", n, iters, setup, "linalg.matrix_power(A, 3)",
              lambda G=G: (G,), lambda o, i: float(np.linalg.matrix_power(o[0], 3)[0, 0]),
              extract="[0, 0]")

    # ---- trace / Frobenius norm (cheap O(n²) reductions) ----
    for n, iters in [(32, 100000), (256, 20000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("trace", n, iters, setup, "linalg.trace(A)",
              lambda A=A: (A,), lambda o, i: float(np.trace(o[0])), extract="")
    for n, iters in [(32, 100000), (256, 10000)]:
        A = spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
        bench("norm(matrix)", n, iters, setup, "linalg.norm(A)",
              lambda A=A: (A,), lambda o, i: float(np.linalg.norm(o[0])), extract="")

    # ---- outer product / Kronecker product ----
    for n, iters in [(64, 50000), (256, 4000)]:
        u, v = rng.standard_normal(n), rng.standard_normal(n)
        setup = f"let u = ndarray.array({lit(u)})\nlet v = ndarray.array({lit(v)})"
        bench("outer", n, iters, setup, "linalg.outer(u, v)",
              lambda u=u, v=v: (u, v), lambda o, i: float(np.outer(o[0], o[1])[0, 0]),
              extract="[0, 0]")
    for n, iters in [(8, 40000), (16, 8000), (32, 1500)]:
        A, B = spd(n), spd(n)
        setup = f"let A = ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])\n" \
                f"let B = ndarray.reshape(ndarray.array({lit(B)}), [{n}, {n}])"
        bench("kron", n, iters, setup, "linalg.kron(A, B)",
              lambda A=A, B=B: (A, B), lambda o, i: float(np.kron(o[0], o[1])[0, 0]),
              extract="[0, 0]")



def run_ndarray():
    """The elementwise ufuncs the ndarray README publishes. Kept separate from the linalg
    cases because they are a different claim on different operands — one table holding both
    would sit a 64-element sqrt beside a 96x96 matmul as though they were comparable."""
    for n, iters, fn in [(64, 200000, "sqrt"), (16384, 3000, "sqrt"),
                         (16384, 3000, "exp"), (16384, 3000, "sin")]:
        x = np.abs(rng.standard_normal(n)) + 0.5
        setup = f"let X = ndarray.array({lit(x)})"
        bench(f"ndarray.{fn}", n, iters, setup, f"ndarray.{fn}(X)",
              lambda x=x: (x,), lambda o, i, fn=fn: float(getattr(np, fn)(o[0])[0]))

    # Array + scalar is the OPERATOR, not add(): ndarray.add takes two arrays (ndarray.hpp:993),
    # while the scalar broadcast is operator+ (ndarray.hpp:1365). The ndarray README billed this
    # row as "16384-element array + scalar" against `ndarray.add`, which is not a form that
    # exists — measure what the code actually provides and name it accordingly.
    x = rng.standard_normal(16384)
    setup = f"let X = ndarray.array({lit(x)})"
    bench("X + scalar", 16384, 3000, setup, "X + 1.5",
          lambda x=x: (x,), lambda o, i: float((o[0] + 1.5)[0]))

    # And the two-array form, which is what ndarray.add really is.
    y = rng.standard_normal(16384)
    setup2 = f"let X = ndarray.array({lit(x)})\nlet Y = ndarray.array({lit(y)})"
    bench("ndarray.add", 16384, 3000, setup2, "ndarray.add(X, Y)",
          lambda x=x, y=y: (x, y), lambda o, i: float((o[0] + o[1])[0]))


if __name__ == "__main__":
    main()
