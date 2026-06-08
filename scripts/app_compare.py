#!/usr/bin/env python3
"""Application-scale cheatah-vs-CPython benchmarks.

Beyond the per-call micro-benchmarks (perf_compare.py), these are whole little
PROGRAMS — the kind of compute a scientist actually writes: a fractal, a numerical
integral, a chaotic ODE integrator (RK4), and an N-body gravitational step. Each is
written in the SAME algorithm in cheatah (.purr) and Python, runs a real workload,
brackets the compute with a monotonic clock, and prints a result we **cross-check**
between the two languages (same algorithm → same answer) so nobody can accuse the
benchmark of comparing different work. The compute is non-trivial and its result is
printed, so nothing is optimized away.

    python3 scripts/app_compare.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
TRIALS = 3

# Each app: a cheatah program and a Python program implementing the SAME algorithm.
# Both print `<result>` then `<elapsed seconds>` (compute only, excluding startup).
APPS = {
    # Mandelbrot set: escape-time over a W×H grid, summing the iteration counts.
    # Pure scalar nested loops — exactly what an interpreter is slowest at.
    "Mandelbrot (800x600, 256)": dict(
        cheatah=r"""import io
import time
let W = 800
let H = 600
let maxit = 256
let total = 0
let t0 = time.monotonic()
for py in range(0, H) {
    for px in range(0, W) {
        let x0 = -2.5 + 3.5 * px / W
        let y0 = -1.0 + 2.0 * py / H
        let x = 0.0
        let y = 0.0
        let it = 0
        while x * x + y * y <= 4.0 and it < maxit {
            let xt = x * x - y * y + x0
            y = 2.0 * x * y + y0
            x = xt
            it = it + 1
        }
        total = total + it
    }
}
let t1 = time.monotonic()
io.print(total)
io.print(t1 - t0)
""",
        python=r"""import time
W, H, maxit = 800, 600, 256
total = 0
t0 = time.monotonic()
for py in range(H):
    for px in range(W):
        x0 = -2.5 + 3.5 * px / W
        y0 = -1.0 + 2.0 * py / H
        x = 0.0; y = 0.0; it = 0
        while x * x + y * y <= 4.0 and it < maxit:
            xt = x * x - y * y + x0
            y = 2.0 * x * y + y0
            x = xt
            it += 1
        total += it
t1 = time.monotonic()
print(total)
print(t1 - t0)
""",
    ),
    # Numerical integration: trapezoidal rule for ∫ sin(x)·e^(-x/100) dx over [0,50]
    # with 20M sample points. A transcendental call per iteration.
    "Integral (sin·exp, 20M pts)": dict(
        cheatah=r"""import io
import time
import math
let a = 0.0
let b = 50.0
let n = 20000000
let h = (b - a) / n
let s = 0.5 * (math.sin(a) * math.exp(-0.01 * a) + math.sin(b) * math.exp(-0.01 * b))
let t0 = time.monotonic()
for i in range(1, n) {
    let x = a + h * i
    s = s + math.sin(x) * math.exp(-0.01 * x)
}
let t1 = time.monotonic()
io.print(s * h)
io.print(t1 - t0)
""",
        python=r"""import time, math
a, b, n = 0.0, 50.0, 20000000
h = (b - a) / n
s = 0.5 * (math.sin(a) * math.exp(-0.01 * a) + math.sin(b) * math.exp(-0.01 * b))
t0 = time.monotonic()
for i in range(1, n):
    x = a + h * i
    s = s + math.sin(x) * math.exp(-0.01 * x)
t1 = time.monotonic()
print(s * h)
print(t1 - t0)
""",
    ),
    # Harmonic oscillator x'' = -x integrated with RK4 for 4M steps — the staple ODE
    # integrator kernel (~a chaotic system like Lorenz would also run great here, but
    # its result diverges between any two floating-point implementations, so we report
    # the phase-independent ENERGY 0.5(x²+v²), which RK4 nearly conserves and both
    # languages agree on). ~8 floating ops per derivative, 4 derivatives per step.
    "Oscillator RK4 (4M steps)": dict(
        cheatah=r"""import io
import time
fn fx(x, v) { return v }
fn fv(x, v) { return -x }
let x = 1.0
let v = 0.0
let dt = 0.0005
let n = 4000000
let t0 = time.monotonic()
for i in range(0, n) {
    let k1x = fx(x, v)
    let k1v = fv(x, v)
    let k2x = fx(x + 0.5 * dt * k1x, v + 0.5 * dt * k1v)
    let k2v = fv(x + 0.5 * dt * k1x, v + 0.5 * dt * k1v)
    let k3x = fx(x + 0.5 * dt * k2x, v + 0.5 * dt * k2v)
    let k3v = fv(x + 0.5 * dt * k2x, v + 0.5 * dt * k2v)
    let k4x = fx(x + dt * k3x, v + dt * k3v)
    let k4v = fv(x + dt * k3x, v + dt * k3v)
    x = x + dt * (k1x + 2.0 * k2x + 2.0 * k3x + k4x) / 6.0
    v = v + dt * (k1v + 2.0 * k2v + 2.0 * k3v + k4v) / 6.0
}
let t1 = time.monotonic()
io.print(0.5 * (x * x + v * v))
io.print(t1 - t0)
""",
        python=r"""import time
def fx(x, v): return v
def fv(x, v): return -x
x = 1.0
v = 0.0
dt = 0.0005
n = 4000000
t0 = time.monotonic()
for i in range(n):
    k1x = fx(x, v); k1v = fv(x, v)
    k2x = fx(x + 0.5*dt*k1x, v + 0.5*dt*k1v)
    k2v = fv(x + 0.5*dt*k1x, v + 0.5*dt*k1v)
    k3x = fx(x + 0.5*dt*k2x, v + 0.5*dt*k2v)
    k3v = fv(x + 0.5*dt*k2x, v + 0.5*dt*k2v)
    k4x = fx(x + dt*k3x, v + dt*k3v)
    k4v = fv(x + dt*k3x, v + dt*k3v)
    x = x + dt * (k1x + 2.0*k2x + 2.0*k3x + k4x) / 6.0
    v = v + dt * (k1v + 2.0*k2v + 2.0*k3v + k4v) / 6.0
t1 = time.monotonic()
print(0.5 * (x * x + v * v))
print(t1 - t0)
""",
    ),
    # N-body: direct O(N²) gravitational acceleration + leapfrog, 256 bodies, 200
    # steps. Lists, nested loops, a sqrt per pair — a real simulation kernel.
    "N-body (256 bodies, 200 steps)": dict(
        cheatah=r"""import io
import time
import math
let N = 256
let steps = 200
let px: list[float] = []
let py: list[float] = []
let vx: list[float] = []
let vy: list[float] = []
for i in range(0, N) {
    let fi = 1.0 + i
    px.append(math.sin(fi * 1.3))
    py.append(math.cos(fi * 0.7))
    vx.append(0.0)
    vy.append(0.0)
}
let dt = 0.001
let eps = 0.01
let t0 = time.monotonic()
for s in range(0, steps) {
    for i in range(0, N) {
        let ax = 0.0
        let ay = 0.0
        for j in range(0, N) {
            let dx = px[j] - px[i]
            let dy = py[j] - py[i]
            let r2 = dx * dx + dy * dy + eps
            let inv = 1.0 / (r2 * math.sqrt(r2))
            ax = ax + dx * inv
            ay = ay + dy * inv
        }
        vx[i] = vx[i] + dt * ax
        vy[i] = vy[i] + dt * ay
    }
    for i in range(0, N) {
        px[i] = px[i] + dt * vx[i]
        py[i] = py[i] + dt * vy[i]
    }
}
let chk = 0.0
for i in range(0, N) { chk = chk + px[i] + py[i] }
let t1 = time.monotonic()
io.print(chk)
io.print(t1 - t0)
""",
        python=r"""import time, math
N, steps = 256, 200
px = [math.sin((1.0+i)*1.3) for i in range(N)]
py = [math.cos((1.0+i)*0.7) for i in range(N)]
vx = [0.0]*N
vy = [0.0]*N
dt, eps = 0.001, 0.01
t0 = time.monotonic()
for s in range(steps):
    for i in range(N):
        ax = 0.0; ay = 0.0
        pxi = px[i]; pyi = py[i]
        for j in range(N):
            dx = px[j] - pxi
            dy = py[j] - pyi
            r2 = dx*dx + dy*dy + eps
            inv = 1.0 / (r2 * math.sqrt(r2))
            ax += dx * inv
            ay += dy * inv
        vx[i] += dt * ax
        vy[i] += dt * ay
    for i in range(N):
        px[i] += dt * vx[i]
        py[i] += dt * vy[i]
chk = 0.0
for i in range(N):
    chk += px[i] + py[i]
t1 = time.monotonic()
print(chk)
print(t1 - t0)
""",
    ),
}


def parse(stdout):
    lines = [l for l in stdout.strip().splitlines() if l.strip()]
    if len(lines) < 2:
        return None, None
    try:
        return float(lines[-1]), lines[-2]
    except ValueError:
        return None, None


def same(a, b):
    if a == b:
        return True
    try:
        fa, fb = float(a), float(b)
        # cheatah's io.print emits ~6 significant figures, so a 1e-4 relative match is
        # the tightest cross-check the printed values support.
        return abs(fa - fb) <= 1e-4 * max(1.0, abs(fa), abs(fb))
    except (ValueError, TypeError):
        return False


def main():
    if not (os.path.exists(PURRC) and os.path.exists(CHEATAH)):
        sys.exit("app_compare: build the `release` preset first (need purrc + cheatah).")
    print(f"# cheatah (compiled) vs CPython {sys.version_info.major}."
          f"{sys.version_info.minor} (interpreted) — whole-program workloads\n")
    print(f"{'application':<32}{'cheatah':>11}{'cpython':>11}{'speedup':>10}  result-match")
    print("-" * 78)
    for name, app in APPS.items():
        # cheatah: compile once, run TRIALS times, keep the min compute time
        ct, c_res = None, None
        with tempfile.TemporaryDirectory() as d:
            purr, so = os.path.join(d, "a.purr"), os.path.join(d, "a.so")
            open(purr, "w").write(app["cheatah"])
            if subprocess.run([PURRC, purr, "-o", so],
                              capture_output=True).returncode == 0:
                for _ in range(TRIALS):
                    t, r = parse(subprocess.run([CHEATAH, so], capture_output=True,
                                                text=True).stdout)
                    if t is not None and (ct is None or t < ct):
                        ct, c_res = t, r
        # python
        pt, p_res = None, None
        for _ in range(TRIALS):
            t, r = parse(subprocess.run([sys.executable, "-c", app["python"]],
                                        capture_output=True, text=True).stdout)
            if t is not None and (pt is None or t < pt):
                pt, p_res = t, r
        if ct is None or pt is None:
            print(f"{name:<32}{'(run failed)':>33}")
            continue
        match = "✓" if same(c_res, p_res) else f"✗ ({c_res} vs {p_res})"
        print(f"{name:<32}{ct*1e3:>8.1f} ms{pt*1e3:>8.1f} ms{pt/ct:>8.1f}×  {match}")
    print("\nSame algorithm both sides; compute time only (startup excluded); result "
          "cross-checked.\ncheatah compiles to native code with NO garbage collector — "
          "memory is reference-counted\nand scope-bound (RAII), so there are no GC "
          "pauses in these loops.")


if __name__ == "__main__":
    main()
