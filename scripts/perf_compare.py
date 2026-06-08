#!/usr/bin/env python3
"""HONEST cheatah-vs-CPython performance comparison.

The point of this harness is to answer one question without fooling ourselves:
**is compiled cheatah actually faster than interpreted CPython on the same work?**

That is harder than it looks, because an optimizing C++ compiler will happily make
cheatah "win" by doing *nothing*: if a loop body's result is unused it is deleted
(dead-code elimination), a constant call is computed once (constant folding), and a
loop-invariant call is hoisted out (LICM). A naive `for _ in range(N): f(x)` loop
measured cheatah at "0 ns/call, 125000× faster" — because the compiler deleted the
loop entirely. That number is a lie.

So every case here is written to be **elision-proof**, identically on both sides:
  1. the input VARIES each iteration (depends on the loop index)   → no folding/LICM
  2. each result is ACCUMULATED into a running total               → the work matters
  3. the total is PRINTED after the loop                           → no DCE
The two programs run the *same algorithm*; cheatah compiles it to native code and
CPython interprets it. The gap is the honest compiled-vs-interpreted difference for
"run this loop". We also print both accumulators so you can eyeball that they match
(same algorithm → same result), and we sanity-check each cheatah loop against an
empty loop so an accidentally-elided body is reported, not silently counted.

Usage (after a `release` build exists):
    python3 scripts/perf_compare.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
DEFAULT_ITERS = 5_000_000
TRIALS = 5  # take the min over several trials to shed OS jitter

# Each case is a loop that VARIES its input by the loop index `i`, ACCUMULATES into
# `acc`, and is PRINTED — so the compiler cannot delete, fold, or hoist the body.
# `imports` are added on both sides (cheatah `import x` == Python `import x` here).
# `pure` cases have no Python-mapping question — they ARE the same source on both
# sides. Cases with a distinct py_body still run the identical algorithm.
CASES = {
    # Pure compiled-vs-interpreted loop: the logistic map x -> r·x·(1-x). Each step
    # depends on the last (chaotic, no closed form, not vectorizable), so the body
    # genuinely runs every iteration. The headline "tight numeric loop" number.
    "loop (logistic map)": dict(
        imports="",
        acc_init="0.5",
        body="acc = 3.9 * acc * (1.0 - acc)",
        py_body="acc = 3.9 * acc * (1.0 - acc)",
    ),
    # A real transcendental call on a varying input — math.sqrt in a loop.
    "math.sqrt(varying)": dict(
        imports="math",
        acc_init="0.0",
        body="acc = acc + math.sqrt(1.0 + i)",
        py_body="acc = acc + math.sqrt(1.0 + i)",
    ),
    # A floating accumulation with two transcendental calls (sqrt + a divide).
    "math: sqrt+divide": dict(
        imports="math",
        acc_init="0.0",
        body="acc = acc + math.sqrt(1.0 + i) / (2.0 + i)",
        py_body="acc = acc + math.sqrt(1.0 + i) / (2.0 + i)",
    ),
    # Hashing: both sides ultimately hash in C (cheatah's hashlib and CPython's are
    # both native), so we EXPECT roughly parity — an honest "we don't win when the
    # work is already native" data point. Input varies via io.str(i); we accumulate a
    # byte of the digest so the hash genuinely has to be computed.
    "hashlib.sha256(varying)": dict(
        imports="hashlib",
        acc_init="0",
        body='acc = acc + ord(hashlib.sha256(io.str(i))[0])',
        py_body='acc = acc + ord(hashlib.sha256(str(i).encode()).hexdigest()[0])',
        iters=500_000,
    ),
}


def _same_number(a, b):
    """Cross-check the two accumulators — equal as ints, or as floats within a
    relative tolerance (cheatah's io.print uses fewer significant digits than
    Python's repr, so the strings differ even when the value is the same)."""
    if a == b:
        return True
    try:
        fa, fb = float(a), float(b)
        return abs(fa - fb) <= 1e-6 * max(1.0, abs(fa), abs(fb))
    except ValueError:
        return False


def _run(argv, **kw):
    return subprocess.run(argv, capture_output=True, text=True, **kw)


def _last_two_floats(stdout):
    """Programs print `acc` then elapsed-seconds; return (elapsed, acc_str)."""
    lines = [l for l in stdout.strip().splitlines() if l.strip()]
    if len(lines) < 2:
        return None, None
    try:
        return float(lines[-1]), lines[-2]
    except ValueError:
        return None, None


def time_cheatah(case, iters, body):
    imports = case.get("imports", "")
    imp = "".join(f"import {m}\n" for m in imports.split() ) if imports else ""
    src = (
        "import io\nimport time\n" + imp +
        f"let acc = {case['acc_init']}\n"
        "let t0 = time.monotonic()\n"
        f"for i in range(0, {iters}) {{\n    {body}\n}}\n"
        "let t1 = time.monotonic()\n"
        "io.print(acc)\n"
        "io.print(t1 - t0)\n"
    )
    with tempfile.TemporaryDirectory() as d:
        purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
        with open(purr, "w") as f:
            f.write(src)
        if _run([PURRC, purr, "-o", so]).returncode != 0:
            return None, None
        best, acc = None, None
        for _ in range(TRIALS):
            t, a = _last_two_floats(_run([CHEATAH, so]).stdout)
            if t is not None and (best is None or t < best):
                best, acc = t, a
        return best, acc


def time_python(case, iters):
    setup = case.get("py_setup", "")
    imports = case.get("imports", "")
    if not setup and imports:
        # default: same imports as cheatah (only when there's no custom py_setup)
        setup = "".join(f"import {m}\n" for m in imports.split()
                        if m not in ("io",))
    body = case["py_body"]
    src = (
        "import time\n" + setup + "\n"
        f"acc = {case['acc_init']}\n"
        "t0 = time.monotonic()\n"
        f"for i in range({iters}):\n    {body}\n"
        "t1 = time.monotonic()\n"
        "print(acc)\n"
        "print(t1 - t0)\n"
    )
    best, acc = None, None
    for _ in range(TRIALS):
        t, a = _last_two_floats(_run([sys.executable, "-c", src]).stdout)
        if t is not None and (best is None or t < best):
            best, acc = t, a
    return best, acc


def time_cheatah_empty(case, iters):
    """Same loop with a no-op body, to detect if the real body got optimized away."""
    return time_cheatah(case, iters, body="acc = acc")[0]


def main():
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("perf_compare: build the `release` preset first (need purrc + cheatah).")
    print(f"# cheatah (compiled) vs CPython {sys.version_info.major}."
          f"{sys.version_info.minor} (interpreted) — same algorithm, "
          f"elision-proof (vary input + accumulate + print)\n")
    print(f"{'workload':<26}{'cheatah':>13}{'cpython':>13}{'speedup':>10}  note")
    print("-" * 78)
    for name, case in CASES.items():
        iters = case.get("iters", DEFAULT_ITERS)
        ct, c_acc = time_cheatah(case, iters, case["body"])
        if ct is None:
            print(f"{name:<26}{'(cheatah compile/run failed)':>40}")
            continue
        empty = time_cheatah_empty(case, iters)
        ch_ns = ct / iters * 1e9
        pt, p_acc = time_python(case, iters)
        # Elision guard: if the real loop is within 30% of an empty loop, the body
        # was (largely) optimized away — flag it instead of reporting a fake win.
        note = ""
        if empty is not None and ct < empty * 1.3:
            note = "⚠ body may be elided (≈ empty loop)"
        elif c_acc is not None and p_acc is not None and not _same_number(c_acc, p_acc):
            note = f"⚠ results differ (cheatah {c_acc} vs py {p_acc})"
        if pt is None:
            print(f"{name:<26}{ch_ns:>10.1f} ns{'—':>13}{'—':>10}  {note}")
        else:
            py_ns = pt / iters * 1e9
            print(f"{name:<26}{ch_ns:>10.1f} ns{py_ns:>10.1f} ns"
                  f"{pt/ct:>8.1f}×  {note}")
    print("\nNotes: 'speedup' is whole-loop wall-clock (compiled vs interpreted) for "
          "the SAME algorithm.\nhashlib is expected near 1× — both sides hash in C; "
          "the gap there is just loop overhead.")


if __name__ == "__main__":
    main()
