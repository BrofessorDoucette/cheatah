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


def C(body, py=None, imp="", setup="", py_setup=None, acc="0.0", iters=10_000_000,
      vs="cpython"):
    """A compared/cheatah-only case. `body`/`py` accumulate into `acc` per iteration.
    py=None → cheatah-only row (no honest Python twin). `vs` is the comparison target
    label ("cpython" or "numpy") — numeric modules with a NumPy equivalent compare
    against NumPy (the fast baseline), not pure Python."""
    return dict(kind="compared" if py else "cheatah_only", body=body, py=py, imp=imp,
                setup=setup, py_setup=py_setup, acc=acc, iters=iters, vs=vs)


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

# ---- string: cheatah `string.f(s)` vs Python `s.f()` (str methods) --------
# These are opaque library calls (string.cpp), so a fixed input is fine — the call
# runs every iteration. Bool results are consumed via a branch; others via len/+.
_SS, _PSS = 'let s = "Hello, World! 123 hello"', 's = "Hello, World! 123 hello"'
_STR = {
    "upper":      C("acc = acc + len(string.upper(s))", "acc = acc + len(s.upper())"),
    "lower":      C("acc = acc + len(string.lower(s))", "acc = acc + len(s.lower())"),
    "capitalize": C("acc = acc + len(string.capitalize(s))", "acc = acc + len(s.capitalize())"),
    "title":      C("acc = acc + len(string.title(s))", "acc = acc + len(s.title())"),
    "swapcase":   C("acc = acc + len(string.swapcase(s))", "acc = acc + len(s.swapcase())"),
    "strip":      C("acc = acc + len(string.strip(s))", "acc = acc + len(s.strip())"),
    "lstrip":     C("acc = acc + len(string.lstrip(s))", "acc = acc + len(s.lstrip())"),
    "rstrip":     C("acc = acc + len(string.rstrip(s))", "acc = acc + len(s.rstrip())"),
    "replace":    C('acc = acc + len(string.replace(s, "l", "L"))', 'acc = acc + len(s.replace("l", "L"))'),
    "center":     C("acc = acc + len(string.center(s, 40))", "acc = acc + len(s.center(40))"),
    "ljust":      C("acc = acc + len(string.ljust(s, 40))", "acc = acc + len(s.ljust(40))"),
    "rjust":      C("acc = acc + len(string.rjust(s, 40))", "acc = acc + len(s.rjust(40))"),
    "zfill":      C("acc = acc + len(string.zfill(s, 40))", "acc = acc + len(s.zfill(40))"),
    "count":      C('acc = acc + string.count(s, "l")', 'acc = acc + s.count("l")'),
    "find":       C('acc = acc + string.find(s, "World")', 'acc = acc + s.find("World")'),
    "rfind":      C('acc = acc + string.rfind(s, "l")', 'acc = acc + s.rfind("l")'),
    "split":      C("acc = acc + len(string.split(s))", "acc = acc + len(s.split())"),
    "splitlines": C("acc = acc + len(string.splitlines(s))", "acc = acc + len(s.splitlines())"),
    "startswith": C('if string.startswith(s, "Hello") { acc = acc + 1.0 }', "acc = acc + (1.0 if s.startswith('Hello') else 0.0)"),
    "endswith":   C('if string.endswith(s, "hello") { acc = acc + 1.0 }', "acc = acc + (1.0 if s.endswith('hello') else 0.0)"),
    "contains":   C('if string.contains(s, "World") { acc = acc + 1.0 }', "acc = acc + (1.0 if 'World' in s else 0.0)"),
    "isalnum":    C("if string.isalnum(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.isalnum() else 0.0)"),
    "isalpha":    C("if string.isalpha(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.isalpha() else 0.0)"),
    "isdigit":    C("if string.isdigit(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.isdigit() else 0.0)"),
    "islower":    C("if string.islower(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.islower() else 0.0)"),
    "isupper":    C("if string.isupper(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.isupper() else 0.0)"),
    "isspace":    C("if string.isspace(s) { acc = acc + 1.0 }", "acc = acc + (1.0 if s.isspace() else 0.0)"),
}
for k, v in _STR.items():
    v.update(imp="string", setup=_SS, py_setup=_PSS, iters=2_000_000)
    REG[f"string.{k}"] = v
# join + capwords need their own operands.
REG["string.join"] = C('acc = acc + len(string.join(", ", parts))', 'acc = acc + len(", ".join(parts))',
                        imp="string", setup='let parts = ["alpha", "beta", "gamma", "delta"]',
                        py_setup='parts = ["alpha", "beta", "gamma", "delta"]', iters=2_000_000)
REG["string.capwords"] = C("acc = acc + len(string.capwords(s))", "acc = acc + len(string.capwords(s))",
                           imp="string", setup=_SS, py_setup="import string\n" + _PSS, iters=2_000_000)

# ---- statistics: vs NumPy (the fast numeric baseline; Python's `statistics`
# module uses slow exact arithmetic, so it's not the honest comparison) -------
_XS_LIST = "[4.0, 8.0, 15.0, 16.0, 23.0, 42.0, 1.0, 2.0, 3.0, 5.0, 7.0, 11.0, 13.0, 17.0, 19.0, 29.0]"
_XS = f"let xs = {_XS_LIST}"
# cheatah call -> NumPy equivalent. statistics.hpp is header-only, so a constant list
# folds; we feed `acc` back into xs[0] (a loop-carried dependency, bounded via fmod)
# so the O(n) reduction genuinely runs every iteration on BOTH sides.
_STAT = {
    "mean":      ("statistics.mean(xs)",      "np.mean(xs)"),
    "median":    ("statistics.median(xs)",    "np.median(xs)"),
    "pstdev":    ("statistics.pstdev(xs)",    "np.std(xs)"),
    "pvariance": ("statistics.pvariance(xs)", "np.var(xs)"),
    "stdev":     ("statistics.stdev(xs)",     "np.std(xs, ddof=1)"),
    "variance":  ("statistics.variance(xs)",  "np.var(xs, ddof=1)"),
    "sum":       ("statistics.sum(xs)",       "np.sum(xs)"),
    "count":     ("statistics.count(xs)",     "xs.size"),
}
for k, (call, npcall) in _STAT.items():
    REG[f"statistics.{k}"] = C(
        f"xs[0] = math.fmod(acc, 100.0) + 1.0\n    acc = acc + {call}",
        f"xs[0] = math.fmod(acc, 100.0) + 1.0; acc = acc + {npcall}",
        imp="statistics math", setup=_XS,
        py_setup=f"import numpy as np\nimport math\nxs = np.array({_XS_LIST})",
        iters=1_000_000, vs="numpy")

# ---- hashlib / html: opaque library calls with clean twins ----------------
REG["hashlib.sha256"] = C("acc = acc + len(hashlib.sha256(io.str(i)))",
                          "acc = acc + len(hashlib.sha256(str(i).encode()).hexdigest())",
                          imp="hashlib", iters=500_000)
REG["parsers.html.escape"] = C("acc = acc + len(parsers.html.escape(s))", "acc = acc + len(html.escape(s))",
                       imp="parsers.html", setup='let s = "<a href=\\"x\\">A & B < C > D</a>"',
                       py_setup='import html\ns = "<a href=\\"x\\">A & B < C > D</a>"', iters=2_000_000)
REG["parsers.html.unescape"] = C("acc = acc + len(parsers.html.unescape(s))", "acc = acc + len(html.unescape(s))",
                         imp="parsers.html", setup='let s = "A &amp; B &lt; C &gt; D &quot;q&quot;"',
                         py_setup='import html\ns = "A &amp; B &lt; C &gt; D &quot;q&quot;"', iters=2_000_000)

# ---- os.path: path-string ops (opaque, clean twins) ----------------------
_P, _PP = 'let p = "/usr/local/bin/python3.12"', 'import os.path\np = "/usr/local/bin/python3.12"'
for k in ["basename", "dirname", "normpath", "abspath"]:
    REG[f"os.path.{k}"] = C(f"acc = acc + len(os.path.{k}(p))", f"acc = acc + len(os.path.{k}(p))",
                            imp="os", setup=_P, py_setup=_PP, iters=2_000_000)
REG["os.path.join"] = C('acc = acc + len(os.path.join("/usr/local", "bin"))',
                        'acc = acc + len(os.path.join("/usr/local", "bin"))',
                        imp="os", py_setup="import os.path", iters=2_000_000)
for k in ["exists", "isfile", "isdir"]:   # stat() syscall each call
    REG[f"os.path.{k}"] = C(f'if os.path.{k}("/usr/bin") {{ acc = acc + 1.0 }}',
                            f'acc = acc + (1.0 if os.path.{k}("/usr/bin") else 0.0)',
                            imp="os", py_setup="import os.path", iters=500_000)
REG["os.path.getsize"] = C('acc = acc + os.path.getsize("/etc/passwd")',
                           'acc = acc + os.path.getsize("/etc/passwd")',
                           imp="os", py_setup="import os.path", iters=500_000)
REG["os.path.splitext"] = NOTE("returns a (root, ext) pair — not reduced to one scalar here")

# ---- os: read-only syscalls (clean twins); mutating ones are noted above --
REG["os.getpid"] = C("acc = acc + os.getpid()", "acc = acc + os.getpid()", imp="os", py_setup="import os", iters=2_000_000)
REG["os.getcwd"] = C("acc = acc + len(os.getcwd())", "acc = acc + len(os.getcwd())", imp="os", py_setup="import os", iters=1_000_000)
REG["os.cpu_count"] = C("acc = acc + os.cpu_count()", "acc = acc + os.cpu_count()", imp="os", py_setup="import os", iters=2_000_000)
REG["os.getenv"] = C('acc = acc + len(os.getenv("PATH"))', 'acc = acc + len(os.getenv("PATH"))', imp="os", py_setup="import os", iters=1_000_000)

# ---- datetime: calendar ops over an epoch. format() builds a medium-length
# timestamp string (the natural string workload here); the component extractors run
# on the same representative epoch. vs CPython's datetime (fromtimestamp + strftime /
# the component attributes). ----------------------------------------------------
_DT = 'let e = 1700000000.0\nlet fmt = "%a %b %d %H:%M:%S %Y"'   # -> "Tue Nov 14 22:13:20 2023" (24 chars)
_DTP = 'import datetime as _dt\ne = 1700000000.0\nfmt = "%a %b %d %H:%M:%S %Y"'
REG["datetime.format"] = C('acc = acc + len(datetime.format(e, fmt))',
                           'acc = acc + len(_dt.datetime.fromtimestamp(e).strftime(fmt))',
                           imp="datetime", setup=_DT, py_setup=_DTP, iters=1_000_000)
for k in ["year", "month", "day", "hour", "minute", "second", "weekday"]:
    _pyattr = "weekday()" if k == "weekday" else k
    REG[f"datetime.{k}"] = C(f"acc = acc + datetime.{k}(e)",
                             f"acc = acc + _dt.datetime.fromtimestamp(e).{_pyattr}",
                             imp="datetime", setup=_DT, py_setup=_DTP, iters=2_000_000)
REG["datetime.now"] = C("acc = acc + len(datetime.now())", "acc = acc + len(str(_dt.datetime.now()))",
                        imp="datetime", py_setup="import datetime as _dt", iters=300_000)
REG["datetime.utcnow"] = C("acc = acc + len(datetime.utcnow())", "acc = acc + len(str(_dt.datetime.utcnow()))",
                           imp="datetime", py_setup="import datetime as _dt", iters=300_000)
REG["datetime.today"] = C("acc = acc + len(datetime.today())", "acc = acc + len(str(_dt.date.today()))",
                          imp="datetime", py_setup="import datetime as _dt", iters=300_000)
REG["datetime.timestamp"] = C("acc = acc + datetime.timestamp()", "acc = acc + _dt.datetime.now().timestamp()",
                              imp="datetime", py_setup="import datetime as _dt", iters=500_000)

# ---- random: vs NumPy's random (the fast/vectorized equivalent). NumPy's
# strength is bulk array generation; per-scalar it carries dispatch overhead. -----
_NPR = "import numpy as np\nrng = np.random.default_rng(1)"
REG["random.random"] = C("acc = acc + random.random()", "acc = acc + rng.random()",
                         imp="random", setup="random.seed(1)", py_setup=_NPR, vs="numpy")
REG["random.randint"] = C("acc = acc + random.randint(1, 100)", "acc = acc + int(rng.integers(1, 101))",
                          imp="random", setup="random.seed(1)", py_setup=_NPR, vs="numpy")
REG["random.uniform"] = C("acc = acc + random.uniform(0.0, 1.0)", "acc = acc + rng.uniform(0.0, 1.0)",
                          imp="random", setup="random.seed(1)", py_setup=_NPR, vs="numpy")
REG["random.gauss"] = C("acc = acc + random.gauss(0.0, 1.0)", "acc = acc + rng.normal(0.0, 1.0)",
                        imp="random", setup="random.seed(1)", py_setup=_NPR, vs="numpy")
REG["random.choice"] = C("acc = acc + random.choice(xs)", "acc = acc + rng.choice(xs)",
                         imp="random", setup="random.seed(1)\nlet xs = [10.0, 20.0, 30.0, 40.0, 50.0]",
                         py_setup=_NPR + "\nxs = np.array([10.0, 20.0, 30.0, 40.0, 50.0])", vs="numpy")
REG["random.seed"] = NOTE("one-time initialization — not a hot path")

# ---- time: clock reads (syscall/vDSO); same on both sides -----------------
for k in ["monotonic", "perf_counter", "process_time", "time"]:
    REG[f"time.{k}"] = C(f"acc = acc + time.{k}()", f"acc = acc + time.{k}()",
                         imp="time", py_setup="import time", iters=2_000_000)
for k in ["monotonic_ns", "perf_counter_ns", "time_ns"]:
    REG[f"time.{k}"] = C(f"acc = acc + time.{k}()", f"acc = acc + time.{k}()",
                         imp="time", py_setup="import time", iters=2_000_000)
REG["time.sleep"] = NOTE("blocks for a requested duration — not micro-benchmarked")

# ---- io: str/repr have clean twins; format/input/open/read_file noted ------
REG["io.str"] = C("acc = acc + len(io.str(i))", "acc = acc + len(str(i))", iters=2_000_000)
REG["io.repr"] = C("acc = acc + len(io.repr(1.0 * i))", "acc = acc + len(repr(1.0 * i))", iters=2_000_000)
REG["io.format"] = NOTE("string templating — closest CPython twin (f-strings) isn't a function call")

# ---- builtins: int-input ones measure cleanly; string-input ones inline ----
REG["builtins.hex"] = C("acc = acc + len(hex(i))", "acc = acc + len(hex(i))", iters=5_000_000)
REG["builtins.bin"] = C("acc = acc + len(bin(i))", "acc = acc + len(bin(i))", iters=5_000_000)
REG["builtins.oct"] = C("acc = acc + len(oct(i))", "acc = acc + len(oct(i))", iters=5_000_000)
REG["builtins.chr"] = C("acc = acc + ord(chr(65 + (i - (i / 26) * 26)))",
                        "acc = acc + ord(chr(65 + i % 26))", iters=5_000_000)
for k in ["len", "ord", "ascii", "hash", "bool", "int", "float", "contains", "startswith",
          "endswith", "index", "slice", "range", "append", "to_bool", "to_int", "to_float"]:
    REG[f"builtins.{k}"] = NOTE("header-inlined to ~sub-nanosecond; the win over CPython "
                                "is its eliminated ~60 ns per-call interpreter overhead")

# ---- parsers.html: object/structure returns, not one scalar (datetime is
# benchmarked above against CPython's datetime) -----------------------------
for k in ["get_attr", "has_attr", "parse"]:
    REG[f"parsers.html.{k}"] = NOTE("returns a parse structure — not reduced to one scalar here")

# ---- numeric ops: the honest comparison is vs NumPy (perf page) -----------
for fn in ["cholesky", "cond", "conj_transpose", "det", "dot", "eig", "eigh", "eigvals",
           "eigvalsh", "inner", "inv", "kron", "lstsq", "matmul", "matrix_power",
           "matrix_rank", "norm", "outer", "pinv", "qr", "slogdet", "solve", "svd",
           "trace", "vdot"]:
    REG[f"linalg.{fn}"] = NUMPY()
REG["linalg.simd_features"] = NOTE("queries CPU SIMD support — not a hot path")
REG["linalg.simd_lane_doubles"] = NOTE("queries CPU SIMD width — not a hot path")
for fn in ["abs", "add", "arange", "array", "binary_op", "broadcast_shapes",
           "broadcast_to", "cbrt", "complex", "conj", "cos", "divide", "exp", "full",
           "get", "imag", "is_contiguous", "log", "mean", "mul", "ones", "real",
           "reshape", "scalar", "shape_of", "size_of", "sin", "sqrt", "sub", "sum",
           "tan", "to_string", "zeros"]:
    REG[f"ndarray.{fn}"] = NUMPY()

# ---- I/O / system / network: no honest in-loop Python twin ----------------
for fn in ["input", "open", "read_file", "print"]:
    REG[f"io.{fn}"] = NOTE("I/O-bound — dominated by the OS, not micro-benchmarked")
for fn in ["accept", "bind", "close", "connect", "last_error", "listen", "local_port",
           "recv", "send", "sendall", "set_reuseaddr", "socket", "tcp_connect", "tcp_listen"]:
    REG[f"socket.{fn}"] = NOTE("network/syscall-bound — not micro-benchmarked")
for fn in ["chdir", "listdir", "makedirs", "mkdir", "remove",
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
            row["vs"] = case.get("vs", "cpython")   # comparison target: cpython | numpy
            pt = time_python(case)
            if pt is not None:
                row["compare_ns"] = round(pt / case["iters"] * 1e9, 2)
                row["speedup"] = round(pt / ct, 1)
        results[key] = row
        extra = f"  ⚠ELIDED" if elided else ""
        sp = f"  {row.get('speedup','—')}× vs {row.get('vs','')}" if kind == "compared" else "  (cheatah-only)"
        print(f"{key:<26} {ch_ns:>8.2f} ns{sp}{extra}")

    try:
        import numpy as _np
        npv = _np.__version__
    except Exception:
        npv = "?"
    meta = {"machine": platform.processor() or platform.machine(),
            "cheatah_commit": commit,
            "cpython": f"{sys.version_info.major}.{sys.version_info.minor}."
                       f"{sys.version_info.micro}",
            "numpy": npv,
            "generated": time.strftime("%Y-%m-%d")}
    json.dump({"meta": meta, "functions": results}, open(OUT, "w"), indent=1, sort_keys=True)
    print(f"\nwrote {OUT}  ({len([1 for r in results.values() if r.get('kind')=='compared'])} compared, "
          f"machine={meta['machine']}, cheatah@{commit}, CPython {meta['cpython']})")


if __name__ == "__main__":
    main()
