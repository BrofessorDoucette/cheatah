#!/usr/bin/env python3
"""The cheatah `@perf` benchmark suite — regenerates per-function speed numbers.

This is the PERIODIC suite (NOT part of the QA gate — benchmarks are slow, noisy, and
machine-specific). Run it now and then on a fixed reference machine — after a codegen/
stdlib change, or a new CPython release — and commit the regenerated
`docs/perf_data.json`. The docs generator reads that file and renders a "Performance"
row on every function it covers, so the numbers live in ONE generated, provenance-
tagged file instead of being hand-written into 218 headers.

Each case times a real cheatah `.purr` (compiled, opaque library call) against the
equivalent CPython, ELISION-PROOF: the input varies each iteration, results are
accumulated, and the total is printed — so the compiler can't delete, fold, or hoist
the body (an empty-loop guard flags any case that slips through). Functions with no
honest Python twin get a cheatah-only row; numpy-backed numeric ops point at the
dedicated NumPy comparison on the performance page.

    python3 scripts/perf_suite.py            # run everything, rewrite perf_data.json
    python3 scripts/perf_suite.py math io    # only the named modules (faster iteration)
"""
import json
import os
import platform
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
OUT = os.path.join(ROOT, "docs", "perf_data.json")
TRIALS = 3


def C(body, py=None, imp="", setup="", py_setup=None, acc="0.0", iters=10_000_000):
    """A compared/cheatah-only case. `body`/`py` accumulate into `acc` per iteration.
    py=None → cheatah-only row (no honest Python twin)."""
    return dict(kind="compared" if py else "cheatah_only", body=body, py=py, imp=imp,
                setup=setup, py_setup=py_setup, acc=acc, iters=iters)


def NUMPY():
    """A numeric op whose honest comparison is vs NumPy (see the performance page)."""
    return dict(kind="numpy")


def NOTE(text):
    """A function we deliberately don't micro-benchmark (I/O-bound, blocking, etc.)."""
    return dict(kind="note", note=text)


# ---------------------------------------------------------------------------
# Registry — keyed by the docs name `<module>.<function>`.
# ---------------------------------------------------------------------------
REG = {}

# ---- math: clean Python twins (math module / builtins) --------------------
_MATH = {
    "sqrt": C("acc = acc + math.sqrt(1.0 + i)", "acc = acc + math.sqrt(1.0 + i)"),
    "cbrt": C("acc = acc + math.cbrt(1.0 + i)", "acc = acc + (1.0 + i) ** (1.0/3.0)"),
    "fabs": C("acc = acc + math.fabs(-1.0 * i)", "acc = acc + math.fabs(-1.0 * i)"),
    "floor": C("acc = acc + math.floor(0.5 + 0.001 * i)", "acc = acc + math.floor(0.5 + 0.001 * i)"),
    "ceil": C("acc = acc + math.ceil(0.5 + 0.001 * i)", "acc = acc + math.ceil(0.5 + 0.001 * i)"),
    "trunc": C("acc = acc + math.trunc(0.5 + 0.001 * i)", "acc = acc + math.trunc(0.5 + 0.001 * i)"),
    "round": C("acc = acc + math.round(0.5 + 0.001 * i)", "acc = acc + round(0.5 + 0.001 * i)"),
    "exp": C("acc = acc + math.exp(0.0000001 * i)", "acc = acc + math.exp(0.0000001 * i)"),
    "log": C("acc = acc + math.log(1.0 + i)", "acc = acc + math.log(1.0 + i)"),
    "log10": C("acc = acc + math.log10(1.0 + i)", "acc = acc + math.log10(1.0 + i)"),
    "log2": C("acc = acc + math.log2(1.0 + i)", "acc = acc + math.log2(1.0 + i)"),
    "sin": C("acc = acc + math.sin(0.001 * i)", "acc = acc + math.sin(0.001 * i)"),
    "cos": C("acc = acc + math.cos(0.001 * i)", "acc = acc + math.cos(0.001 * i)"),
    "tan": C("acc = acc + math.tan(0.001 * i)", "acc = acc + math.tan(0.001 * i)"),
    "asin": C("acc = acc + math.asin(math.sin(0.001 * i))", "acc = acc + math.asin(math.sin(0.001 * i))"),
    "acos": C("acc = acc + math.acos(math.sin(0.001 * i))", "acc = acc + math.acos(math.sin(0.001 * i))"),
    "atan": C("acc = acc + math.atan(0.001 * i)", "acc = acc + math.atan(0.001 * i)"),
    "atan2": C("acc = acc + math.atan2(1.0 + i, 2.0)", "acc = acc + math.atan2(1.0 + i, 2.0)"),
    "hypot": C("acc = acc + math.hypot(1.0 * i, 2.0)", "acc = acc + math.hypot(1.0 * i, 2.0)"),
    "fmod": C("acc = acc + math.fmod(1.0 + i, 7.0)", "acc = acc + math.fmod(1.0 + i, 7.0)"),
    "copysign": C("acc = acc + math.copysign(2.0, -1.0 * i)", "acc = acc + math.copysign(2.0, -1.0 * i)"),
    "degrees": C("acc = acc + math.degrees(0.01 * i)", "acc = acc + math.degrees(0.01 * i)"),
    "radians": C("acc = acc + math.radians(0.01 * i)", "acc = acc + math.radians(0.01 * i)"),
    "pow": C("acc = acc + math.pow(1.0 + 0.000001 * i, 2.0)", "acc = acc + math.pow(1.0 + 0.000001 * i, 2.0)"),
    "abs": C("acc = acc + math.abs(-1.0 * i)", "acc = acc + abs(-1.0 * i)"),
    "min": C("acc = acc + math.min(1.0 * i, 2.0)", "acc = acc + min(1.0 * i, 2.0)"),
    "max": C("acc = acc + math.max(1.0 * i, 2.0)", "acc = acc + max(1.0 * i, 2.0)"),
    "gcd": C("acc = acc + math.gcd(i + 1, 48)", "acc = acc + math.gcd(i + 1, 48)", iters=2_000_000),
    "factorial": C("acc = acc + math.factorial(10)", "acc = acc + math.factorial(10)", iters=2_000_000),
    "isnan": C("if math.isnan(1.0 + i) { acc = acc + 1.0 }", "acc = acc + (1.0 if math.isnan(1.0 + i) else 0.0)"),
    "isinf": C("if math.isinf(1.0 + i) { acc = acc + 1.0 }", "acc = acc + (1.0 if math.isinf(1.0 + i) else 0.0)"),
    "isfinite": C("if math.isfinite(1.0 + i) { acc = acc + 1.0 }", "acc = acc + (1.0 if math.isfinite(1.0 + i) else 0.0)"),
}
for k, v in _MATH.items():
    v["imp"] = "math"
    REG[f"math.{k}"] = v

# ---- numeric ops: the honest comparison is vs NumPy (perf page) -----------
for fn in ["cholesky", "cond", "conj_transpose", "det", "dot", "eig", "eigh", "eigvals",
           "eigvalsh", "inner", "inv", "kron", "lstsq", "matmul", "matrix_power",
           "matrix_rank", "norm", "outer", "pinv", "qr", "slogdet", "solve", "svd",
           "trace", "vdot"]:
    REG[f"linalg.{fn}"] = NUMPY()
REG["linalg.simd_features"] = NOTE("queries CPU SIMD support — not a hot path")
REG["linalg.simd_lane_doubles"] = NOTE("queries CPU SIMD width — not a hot path")
for fn in ["add", "arange", "array", "binary_op", "broadcast_shapes", "broadcast_to",
           "complex", "conj", "divide", "full", "get", "imag", "is_contiguous", "mean",
           "mul", "ones", "real", "reshape", "scalar", "shape_of", "size_of", "sub",
           "sum", "to_string", "zeros"]:
    REG[f"ndarray.{fn}"] = NUMPY()

# ---- I/O / system / network: no honest in-loop Python twin ----------------
for fn in ["input", "open", "read_file", "print"]:
    REG[f"io.{fn}"] = NOTE("I/O-bound — dominated by the OS, not micro-benchmarked")
for fn in ["accept", "bind", "close", "connect", "last_error", "listen", "local_port",
           "recv", "send", "sendall", "set_reuseaddr", "socket", "tcp_connect", "tcp_listen"]:
    REG[f"socket.{fn}"] = NOTE("network/syscall-bound — not micro-benchmarked")
for fn in ["chdir", "getcwd", "getenv", "listdir", "makedirs", "mkdir", "remove",
           "rename", "rmdir", "setenv", "system"]:
    REG[f"os.{fn}"] = NOTE("filesystem/process syscall — not micro-benchmarked")


# ---------------------------------------------------------------------------
# Timing
# ---------------------------------------------------------------------------
def _run(argv):
    return subprocess.run(argv, capture_output=True, text=True)


def _imports(imp):
    return "".join(f"import {m}\n" for m in imp.split()) if imp else ""


def time_cheatah(case, body):
    src = ("import io\nimport time\n" + _imports(case["imp"]) + case["setup"] + "\n"
           f"let acc = {case['acc']}\n"
           "let t0 = time.monotonic()\n"
           f"for i in range(0, {case['iters']}) {{\n    {body}\n}}\n"
           "let t1 = time.monotonic()\n"
           "io.print(acc)\nio.print(t1 - t0)\n")
    with tempfile.TemporaryDirectory() as d:
        purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
        open(purr, "w").write(src)
        if _run([PURRC, purr, "-o", so]).returncode != 0:
            return None
        best = None
        for _ in range(TRIALS):
            out = _run([CHEATAH, so]).stdout.strip().splitlines()
            try:
                t = float(out[-1])
            except (IndexError, ValueError):
                return None
            best = t if best is None or t < best else best
        return best


def time_python(case):
    setup = case["py_setup"] if case["py_setup"] is not None else \
        "".join(f"import {m}\n" for m in case["imp"].split() if m != "io")
    src = ("import time\n" + setup + "\n"
           + f"acc = {case['acc']}\n"
           "t0 = time.monotonic()\n"
           f"for i in range({case['iters']}):\n    {case['py']}\n"
           "t1 = time.monotonic()\nprint(acc)\nprint(t1 - t0)\n")
    best = None
    for _ in range(TRIALS):
        out = _run([sys.executable, "-c", src]).stdout.strip().splitlines()
        try:
            t = float(out[-1])
        except (IndexError, ValueError):
            return None
        best = t if best is None or t < best else best
    return best


def main():
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("perf_suite: build the `release` preset first (need purrc + cheatah).")
    only = set(sys.argv[1:])
    commit = _run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"]).stdout.strip()
    results = {}
    existing = {}
    if os.path.exists(OUT):
        existing = json.load(open(OUT)).get("functions", {})
    for key, case in sorted(REG.items()):
        mod = key.split(".")[0]
        if only and mod not in only:
            results[key] = existing.get(key, case if case["kind"] in ("numpy", "note") else {})
            continue
        kind = case["kind"]
        if kind in ("numpy", "note"):
            results[key] = case
            print(f"{key:<26} {kind}")
            continue
        empty = time_cheatah(case, "acc = acc")
        ct = time_cheatah(case, case["body"])
        if ct is None:
            print(f"{key:<26} ⚠ cheatah failed")
            continue
        ch_ns = ct / case["iters"] * 1e9
        elided = empty is not None and ct < empty * 1.3
        row = {"kind": kind, "cheatah_ns": round(ch_ns, 2)}
        if elided:
            row["warn"] = "elided"
        if kind == "compared":
            pt = time_python(case)
            if pt is not None:
                row["python_ns"] = round(pt / case["iters"] * 1e9, 2)
                row["speedup"] = round(pt / ct, 1)
        results[key] = row
        extra = f"  ⚠ELIDED" if elided else ""
        sp = f"  {row.get('speedup','—')}×" if kind == "compared" else "  (cheatah-only)"
        print(f"{key:<26} {ch_ns:>8.2f} ns{sp}{extra}")

    meta = {"machine": platform.processor() or platform.machine(),
            "cheatah_commit": commit,
            "cpython": f"{sys.version_info.major}.{sys.version_info.minor}."
                       f"{sys.version_info.micro}",
            "generated": time.strftime("%Y-%m-%d")}
    json.dump({"meta": meta, "functions": results}, open(OUT, "w"), indent=1, sort_keys=True)
    print(f"\nwrote {OUT}  ({len([1 for r in results.values() if r.get('kind')=='compared'])} compared, "
          f"machine={meta['machine']}, cheatah@{commit}, CPython {meta['cpython']})")


if __name__ == "__main__":
    main()
