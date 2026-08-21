#!/usr/bin/env python3
"""cheatah `linalg` vs NumPy — a FULL per-function dimension sweep.

For every linalg routine with a NumPy equivalent, this times cheatah vs NumPy across
a range of problem sizes and reports **exactly where cheatah crosses from faster to
slower** (NumPy dispatches to BLAS/LAPACK, so cheatah wins small — no Python/dispatch
overhead — and loses large). Same fixed-seed operands fed to both sides, result
consumed, best of several trials.

    python3 scripts/linalg_sweep.py            # console table
    python3 scripts/linalg_sweep.py --md       # emit a Markdown table for the docs

Run after a `release` build. Slow (it compiles a .purr per function×size) — this is a
periodic/manual tool, not part of the QA gate.
"""
import atexit
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
# 7 striated rounds replacing 4 best-of-N trials per side — see scripts/perf_suite.py.
ROUNDS = 7
rng = np.random.default_rng(0)

MAT_SIZES = [2, 4, 8, 16, 32, 64, 128]
VEC_SIZES = [16, 64, 256, 1024, 4096, 16384]


def lit(M):
    return "[" + ", ".join(repr(float(v)) for v in np.asarray(M).flatten()) + "]"


def run(argv):
    return subprocess.run(argv, capture_output=True, text=True)


def parse_last(stdout):
    lines = [l for l in stdout.strip().splitlines() if l.strip()]
    try:
        return float(lines[-1])
    except (IndexError, ValueError):
        return None


_TEMPDIRS = []
atexit.register(lambda: [shutil.rmtree(d, ignore_errors=True) for d in _TEMPDIRS])


def build_cheatah(setup, expr, iters, extract):
    consume = expr if extract == "" else f"ndarray.get({expr}, {extract})"
    src = ("import io\nimport time\nimport ndarray\nimport linalg\n" + setup + "\n"
           "let acc = 0.0\nlet t0 = time.monotonic()\n"
           f"for i in range(0, {iters}) {{\n    acc = acc + {consume}\n}}\n"
           "let t1 = time.monotonic()\nio.print(acc)\nio.print(t1 - t0)\n")
    # Compile ONCE; the .so must outlive this call so the striated rounds can re-run it
    # without paying purrc between the two sides of a pair.
    d = tempfile.mkdtemp(prefix="linalg_sweep.")
    _TEMPDIRS.append(d)
    purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
    open(purr, "w").write(src)
    return so if run([PURRC, purr, "-o", so]).returncode == 0 else None


def _median(xs):
    ys = sorted(xs)
    n = len(ys)
    return ys[n // 2] if n % 2 else 0.5 * (ys[n // 2 - 1] + ys[n // 2])


def np_once(op, iters):
    import time as _t
    a = 0.0
    t0 = _t.monotonic()
    for _ in range(iters):
        a += op()
    return _t.monotonic() - t0


# ---- per-function specs. Each builds operands for size n and returns
#      (cheatah_setup, cheatah_expr, extract, numpy_op). ------------------------
def spd(n):
    A = rng.standard_normal((n, n))
    return A @ A.T + n * np.eye(n)


def M_(n):
    return f"ndarray.reshape(ndarray.array({{}}), [{n}, {n}])"


def matrix_specs(n):
    A = spd(n)
    G = rng.standard_normal((n, n)) * 0.3 + np.eye(n)   # general, well-conditioned
    b = rng.standard_normal(n)
    Bc = rng.standard_normal((n, 1))
    mk = lambda M: f"ndarray.reshape(ndarray.array({lit(M)}), [{n}, {n}])"
    specs = {}
    specs["matmul"] = (f"let A = {mk(A)}", "linalg.matmul(A, A)", "[0, 0]", lambda: float((A @ A)[0, 0]))
    specs["inv"] = (f"let A = {mk(A)}", "linalg.inv(A)", "[0, 0]", lambda: float(np.linalg.inv(A)[0, 0]))
    specs["det"] = (f"let A = {mk(A)}", "linalg.det(A)", "", lambda: float(np.linalg.det(A)))
    specs["slogdet"] = (f"let A = {mk(A)}", "linalg.slogdet(A).logabsdet", "", lambda: float(np.linalg.slogdet(A)[1]))
    specs["cholesky"] = (f"let A = {mk(A)}", "linalg.cholesky(A)", "[0, 0]", lambda: float(np.linalg.cholesky(A)[0, 0]))
    specs["qr"] = (f"let A = {mk(A)}", "linalg.qr(A).r", "[0, 0]", lambda: float(np.linalg.qr(A)[1][0, 0]))
    specs["svd"] = (f"let A = {mk(A)}", "linalg.svd(A).s", "[0]", lambda: float(np.linalg.svd(A, compute_uv=False)[0]))
    specs["pinv"] = (f"let A = {mk(A)}", "linalg.pinv(A)", "[0, 0]", lambda: float(np.linalg.pinv(A)[0, 0]))
    specs["cond"] = (f"let A = {mk(A)}", "linalg.cond(A)", "", lambda: float(np.linalg.cond(A)))
    specs["norm"] = (f"let A = {mk(A)}", "linalg.norm(A)", "", lambda: float(np.linalg.norm(A)))
    specs["trace"] = (f"let A = {mk(A)}", "linalg.trace(A)", "", lambda: float(np.trace(A)))
    specs["matrix_rank"] = (f"let A = {mk(A)}", "linalg.matrix_rank(A)", "", lambda: float(np.linalg.matrix_rank(A)))
    specs["matrix_power"] = (f"let A = {mk(G)}", "linalg.matrix_power(A, 3)", "[0, 0]", lambda: float(np.linalg.matrix_power(G, 3)[0, 0]))
    specs["eigvalsh"] = (f"let A = {mk(A)}", "linalg.eigvalsh(A)", "[0]", lambda: float(np.linalg.eigvalsh(A)[-1]))
    specs["eigh"] = (f"let A = {mk(A)}", "linalg.eigh(A).values", "[0]", lambda: float(np.linalg.eigh(A)[0][-1]))
    specs["eigvals"] = (f"let A = {mk(G)}", "ndarray.real(linalg.eigvals(A))", "[0]", lambda: float(np.sort(np.linalg.eigvals(G).real)[-1]))
    specs["eig"] = (f"let A = {mk(G)}", "ndarray.real(linalg.eig(A).values)", "[0]", lambda: float(np.sort(np.linalg.eig(G)[0].real)[-1]))
    specs["solve"] = (f"let A = {mk(A)}\nlet b = ndarray.array({lit(b)})", "linalg.solve(A, b)", "[0]", lambda: float(np.linalg.solve(A, b)[0]))
    specs["lstsq"] = (f"let A = {mk(A)}\nlet b = ndarray.reshape(ndarray.array({lit(Bc)}), [{n}, 1])", "linalg.lstsq(A, b)", "[0, 0]", lambda: float(np.linalg.lstsq(A, Bc, rcond=None)[0][0, 0]))
    return specs


# `outer` produces an n×n result, so it scales like a matrix — cap it like one
# (VEC_SIZES' 16384 would allocate a 16384×16384 ≈ 2 GB array per call).
OUTER_SIZES = [16, 64, 256, 1024, 4096]


def vector_specs(n):
    u, v = rng.standard_normal(n), rng.standard_normal(n)
    vl = lambda x: f"ndarray.array({lit(x)})"
    specs = {}
    specs["dot"] = (f"let u = {vl(u)}\nlet v = {vl(v)}", "linalg.dot(u, v)", "", lambda: float(u @ v))
    specs["inner"] = (f"let u = {vl(u)}\nlet v = {vl(v)}", "linalg.inner(u, v)", "", lambda: float(np.inner(u, v)))
    specs["vdot"] = (f"let u = {vl(u)}\nlet v = {vl(v)}", "linalg.vdot(u, v)", "", lambda: float(np.vdot(u, v)))
    specs["outer"] = (f"let u = {vl(u)}\nlet v = {vl(v)}", "linalg.outer(u, v)", "[0, 0]", lambda: float(np.outer(u, v)[0, 0]))
    specs["norm_vec"] = (f"let u = {vl(u)}", "linalg.norm(u)", "", lambda: float(np.linalg.norm(u)))
    return specs


# kron blows up (n×n -> n²×n²), so cap its sizes hard.
KRON_SIZES = [2, 4, 8, 16, 32]


def kron_spec(n):
    A = spd(n)
    mk = f"ndarray.reshape(ndarray.array({lit(A)}), [{n}, {n}])"
    return (f"let A = {mk}", "linalg.kron(A, A)", "[0, 0]", lambda: float(np.kron(A, A)[0, 0]))


def iters_for(n, cubic=True):
    base = 2_000_000 // (n * n * (n if cubic else 1))
    return max(300, min(base, 200_000))


def sweep_function(name, size_specs, cubic=True):
    """size_specs: list of (n, (setup, expr, extract, npop)). Returns list of
    (n, cheatah_us, numpy_us, ratio) where ratio = numpy/cheatah (>1 = cheatah faster)."""
    rows = []
    for n, (setup, expr, extract, npop) in size_specs:
        it = iters_for(n, cubic)
        so = build_cheatah(setup, expr, it, extract)
        if so is None:
            rows.append((n, None, None, None))
            continue
        # Striated: cheatah then NumPy inside each round, so a crossover point is never an
        # artefact of one side having been measured while the machine was cooler.
        ch_ts, np_ts, ratios = [], [], []
        for _ in range(ROUNDS):
            ct = parse_last(run([CHEATAH, so]).stdout)
            if ct is None:
                break
            nt = np_once(npop, it)
            ch_ts.append(ct)
            np_ts.append(nt)
            ratios.append(nt / ct)
        if not ratios:
            rows.append((n, None, None, None))
            continue
        cu = _median(ch_ts) / it * 1e6
        nu = _median(np_ts) / it * 1e6
        rows.append((n, cu, nu, _median(ratios)))   # median of PAIRED ratios
    return rows


def crossover(rows):
    """First n where cheatah becomes slower (ratio < 1). Returns (n, prevn) or
    'never'/'always'."""
    prev = None
    for (n, cu, nu, r) in rows:
        if r is None:
            continue
        if r < 1.0:
            return ("cross", n, prev)
        prev = n
    return ("never", None, prev)


# The sweep is long (it compiles a .purr per function×size), so it caches each
# function's rows to disk the moment they're computed and RESUMES from that cache on
# the next run — a killed/interrupted run loses at most the function in flight, and
# re-running simply finishes the rest. Delete the cache to force a clean sweep.
import json

CACHE = os.path.join(ROOT, "scripts", ".linalg_sweep_cache.json")


def load_cache() -> dict:
    try:
        return json.load(open(CACHE))
    except (OSError, ValueError):
        return {}


def save_cache(cache: dict) -> None:
    tmp = CACHE + ".tmp"
    json.dump(cache, open(tmp, "w"))
    os.replace(tmp, CACHE)


def main():
    md = "--md" in sys.argv
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("linalg_sweep: build the `release` preset first.")

    cache = load_cache()  # name -> {"kind": "mat"|"vec", "rows": [[n,cu,nu,r], …]}

    def need(name, kind, size_specs, cubic):
        if name in cache:
            return
        rows = sweep_function(name, size_specs, cubic=cubic)
        cache[name] = {"kind": kind, "rows": rows}
        save_cache(cache)
        print(f"[{name}] done", file=sys.stderr)

    # matrix functions (cubic)
    mat_names = list(matrix_specs(2).keys())
    for name in mat_names:
        need(name, "mat", [(n, matrix_specs(n)[name]) for n in MAT_SIZES], True)
    # vector functions (linear); `outer` produces n×n, so cap it like a matrix.
    vec_names = list(vector_specs(16).keys())
    for name in vec_names:
        sizes = OUTER_SIZES if name == "outer" else VEC_SIZES
        need(name, "vec", [(n, vector_specs(n)[name]) for n in sizes], False)
    # kron (capped)
    need("kron", "mat", [(n, kron_spec(n)) for n in KRON_SIZES], True)

    results = {name: (cache[name]["kind"], cache[name]["rows"])
               for name in (mat_names + ["kron"] + vec_names) if name in cache}

    def fmt_ratio(r):
        if r is None:
            return "—"
        return f"{r:.1f}× faster" if r >= 1 else f"{1/r:.1f}× slower"

    def dim_label(kind, name, n):
        """Full operand dimensions, never just a bare n."""
        if name == "kron":
            return f"{n}×{n} ⊗ {n}×{n} → {n * n}×{n * n}"
        if kind == "vec":
            if name == "outer":
                return f"two {n}-vectors → {n}×{n}"
            return f"{n}-vector"
        if name == "solve":
            return f"{n}×{n} matrix, {n}-vector"
        if name == "lstsq":
            return f"{n}×{n} matrix, {n}×1 rhs"
        return f"{n}×{n} matrix"

    def shape_family(kind, name):
        if name == "kron":
            return "n×n ⊗ n×n"
        if kind == "vec":
            return "two n-vectors → n×n" if name == "outer" else "n-vector"
        if name == "solve":
            return "n×n matrix · n-vector"
        if name == "lstsq":
            return "n×n matrix · n×1 rhs"
        return "n×n matrix"

    if md:
        print("| function | operand | cheatah wins up to | crossover (slower from) | smallest | largest |")
        print("|----------|---------|--------------------|-------------------------|----------|---------|")
        for name in mat_names + ["kron"] + vec_names:
            kind, rows = results[name]
            valid = [r for r in rows if r[3] is not None]
            if not valid:
                continue
            kind_x, xn, prevn = crossover(rows)
            small = valid[0]
            large = valid[-1]
            wins = (dim_label(kind, name, prevn) if (kind_x == "cross" and prevn)
                    else ("all tested" if kind_x == "never" else "—"))
            cross = dim_label(kind, name, xn) if kind_x == "cross" else "— (wins throughout)"
            disp = name.replace("norm_vec", "norm (vec)")
            print(f"| `{disp}` | {shape_family(kind, name)} "
                  f"| {wins} | {cross} "
                  f"| {fmt_ratio(small[3])} at {dim_label(kind, name, small[0])} "
                  f"| {fmt_ratio(large[3])} at {dim_label(kind, name, large[0])} |")
        return

    for name in mat_names + ["kron"] + vec_names:
        kind, rows = results[name]
        print(f"\n{name}:")
        for (n, cu, nu, r) in rows:
            if r is None:
                print(f"  n={n:<5} (failed)")
            else:
                w = "cheatah" if r >= 1 else "numpy  "
                print(f"  n={n:<5} cheatah={cu:>9.2f}us  numpy={nu:>9.2f}us  {w} {max(r,1/r):>5.1f}x")


if __name__ == "__main__":
    main()
