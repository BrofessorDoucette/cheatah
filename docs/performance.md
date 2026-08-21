# Performance {#performance}

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

cheatah exists to run **Python-shaped code at hand-written-C++ speed** — a design
constraint we apply *a priori*, before writing a feature, not an afterthought to
profile toward later. This page records how we keep cheatah fast, and the one
deliberate price we pay: **slower compilation**.

## The reference machine {#reference-machine}

Every wall-clock number on this page is **machine-specific**, so here is the machine. The
page used to say "the reference machine" without ever naming it, which made the numbers
impossible to reproduce or to argue with.

**Reference machine.** Intel Core i7-12700H (6 performance + 8 efficiency cores, 20 threads,
4.6 GHz max), 62 GB RAM, Linux 7.0.11-76070011, Clang 18.1.3, everything built
`-O3 -march=native`. CPU frequency scaling is **enabled** — this is a laptop part, so
absolute figures sit slightly below what a fixed-clock desktop would give, and comparisons
are always taken against a competitor measured in the same session on the same silicon.
CPython 3.12.3, NumPy 1.26.4, GLM 0.9.9.8, Eigen 3.4.0, OpenSSL 3.0.13.

**How a competitor comparison is measured.** Both sides are timed in the **same run**, with
their repetitions **interleaved** rather than taken as two consecutive blocks, so clock and
thermal drift over the measurement window becomes zero-mean noise instead of a systematic
tilt in favour of whichever side ran first. Each row is the **median** over repetitions with
its spread reported — never a best-of-N minimum, which flatters both sides unevenly and hides
how noisy a case was. A difference is only called a win or a loss when it clears **both** a
1.15× ratio and a 0.25 ns absolute gap; below that it is reported as parity, because on
sub-nanosecond operations the harness's own scaffolding is a larger effect than the code.

Generated tables carry a `cheatah-bench-stamp` comment recording the date, commit, host,
compiler, competitor versions, repetition count and statistic used. If a table has no stamp,
it has not yet been re-measured under this methodology — and it says so.

Numbers that cannot be produced on this machine are **not** removed and their features are
**not** removed either: macOS and AArch64 are supported, built and tested in CI, and their
performance figures are simply marked as not measured here rather than quietly invented.

## The core bargain: compile-time cost for run-time speed

A `.purr` program is transpiled to modern C++ and built at <b>`-O3 -march=native`</b>,
then run on the headless host. We lean hard on **templates and C++20 concepts** for
zero-cost abstraction — generic code that monomorphizes at the call site into exactly
the machine code you'd write by hand, with no virtual dispatch, no boxing, no runtime
type tags.

Templates aren't free — they make the compiler work harder, so **cheatah compiles
more slowly than a dynamically-typed language would "load."** We take that trade
knowingly: compilation happens once, but the `.so` then runs its hot loops millions of
times. Paying at build time to delete the cost forever at run time is the right side of
that trade for the scientific-computing, ML, and systems workloads cheatah targets.
(It's also why every emitted template is **concept-constrained** — see
@ref constrain-all-templates — so the extra compiler work also buys *comprehensible
errors*.)

## Whole programs: cheatah vs CPython {#vs-cpython-apps}

Micro-benchmarks are easy to game; whole programs are not. Here are four *complete*
programs — the kind of compute a scientist actually writes — in the **same algorithm**
in cheatah and CPython, with the result **cross-checked between the two languages** so
the comparison is honest. The harness is
[`scripts/app_compare.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/app_compare.purr);
times are compute-only (startup excluded). Each program is measured over **7 striated
rounds** — one round times the cheatah build and then the CPython build before either is
repeated — so drift over the session cannot favour one side. Every row reports the
**median of the per-round ratios**, with the full spread of those ratios in brackets:

<!-- BENCH:whole-programs begin -->
<!-- cheatah-bench-stamp v1
     suite:        whole-programs
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  CPython 3.12
     harness:      rounds=7, striated (every configuration runs once per round)
     statistic:    median of per-round PAIRED ratios; [lo–hi] is the range of those ratios
     watch:        compiler/, runtime/, scripts/bench/, scripts/app_compare.purr
     publishable:  true

     PRODUCED BY:
       bash scripts/bench/build-harness.sh scripts/app_compare.purr /tmp/ac.so && cheatah /tmp/ac.so docs/bench/whole-programs.md
-->

| program | what it does | cheatah | CPython | speedup |
|---------|--------------|--------:|--------:|--------:|
| **Mandelbrot** | escape-time over an 800×600 grid (≤256 iters) | 61 ms | 4451 ms | **74×** [66–77] |
| **Numerical integral** | trapezoid ∫ sin(x)·e^(−x/100), 20M points | 175 ms | 2482 ms | **14×** [13–18] |
| **RK4 ODE** | 4th-order Runge–Kutta, 4 000 000 steps | 44 ms | 2663 ms | **61×** [45–63] |
| **N-body** | direct O(N²) gravity, 256 bodies × 200 leapfrog steps | 32 ms | 2963 ms | **89×** [78–106] |
<!-- BENCH:whole-programs end -->

Read the brackets, not just the bold number. The N-body ratio moves between 67× and 97×
across rounds — almost a 1.5× swing — because CPython's own run-to-run variance on this
workload is large (its times ranged 271 ms wide). The previous version of this table
published a single **96×** for that row, which we can now see sat at the very top of the
range rather than in the middle of it. The integral row, by contrast, is genuinely tight
at 13–15×, and that stability is itself informative: most of its time is in `libm`, which
behaves the same on both sides.

These aren't cherry-picked kernels handed to a C extension — they are *the program*,
loops and all, the part CPython interprets one bytecode at a time. The integral case is
"lowest" at 13× only because most of its time is inside `libm`'s `sin`/`exp` (native on
both sides); the pure-Python-logic programs land at **62–87×** by median. (A *chaotic* system like
Lorenz runs just as fast, but its final state diverges between floating-point
implementations — `-march=native` uses FMA, CPython doesn't — so we integrate a harmonic
oscillator and cross-check its conserved **energy** instead.)

## Dogfooding: cheatah generates its own docs {#vs-cpython-docgen}

A different shape of workload — real tooling, not a compute kernel. This very site is
**generated by a cheatah program**: [`gen.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen-cheatah/gen.purr)
(~1,500 lines) is the 1:1 port of the original Python generator,
[`generate.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen/generate.py)
(~1,200 lines), which began as this benchmark's baseline.

**The two generators are no longer equivalent, so there is no full-site comparison here.**
This page used to claim they were held byte-identical — `diff -r` empty across all 213
generated files — and to publish a 389 ms vs 152 ms (2.6×) whole-site table on that basis.
Re-checked by running both: `gen.purr` writes **292** pages and `generate.py` writes
**252**, and *every one of the 252 shared pages differs*. The Python twin was never updated
for the package switcher, the Biome Standard page, or the extension **subsites** — it still
renders `cheatah-gpu`, `cheatah-plot` and the rest as flat namespaces in the main sidebar.
Whatever the two timings measured, it was not the same program doing the same work, so the
ratio is withdrawn rather than restated.

What survives is the render-kernel comparison below, and it is the stronger measurement
anyway: `gen_bench.purr` and `gen_bench.py` are small programs written to do **identical**
work, and their equivalence is checked by comparing their output rather than assumed.

Restoring a whole-site number means first restoring parity — porting `generate.py` forward
to subsites and re-checking `diff -r` — and it is tracked as that, not as a number waiting
to be refreshed.

### The render kernel, isolated (and parallelized)

The distilled render pass exists in both languages too:
[`gen_bench.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen-cheatah/gen_bench.purr)
(using the from-scratch [`parsers.xml`](namespacecheatah_1_1parsers_1_1xml.html) reader) and
[`gen_bench.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen-cheatah/gen_bench.py)
(using CPython's C-accelerated `xml.etree`). Each parses **all 52 namespace XML files** and
renders every module's members to HTML, compute-only. The three are driven by
[`gen_bench_compare.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen-cheatah/gen_bench_compare.purr),
which runs **all three inside each of 9 rounds** so none of them is measured as its own
consecutive block:

<!-- BENCH:render-kernel begin -->
<!-- cheatah-bench-stamp v1
     suite:        render-kernel
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  CPython 3.12 (xml.etree / expat)
     harness:      rounds=9, striated (every configuration runs once per round)
     statistic:    median wall clock; speedup = median of per-round PAIRED ratios
     watch:        stdlib/parsers/xml/, docs/gen-cheatah/gen_bench.purr, docs/gen-cheatah/gen_bench_parallel.purr, docs/gen-cheatah/gen_bench_compare.purr
     publishable:  true

     PRODUCED BY:
       bash scripts/bench/build-harness.sh docs/gen-cheatah/gen_bench_compare.purr /tmp/gbc.so && cheatah /tmp/gbc.so docs/bench/render-kernel.md
-->

| generator | median | spread (σ) | vs CPython |
|-----------|-------:|-----------:|-----------:|
| CPython (`xml.etree`, native `expat`) | 24.4 ms | ±0.4 ms | 1.0× |
| cheatah, single-threaded (`gen_bench.purr`) | 12.5 ms | ±1.1 ms | **1.94×** |
| cheatah, 4 threads over a shared `memory.Owner` (`gen_bench_parallel.purr`) | 5.9 ms | ±0.2 ms | **4.14×** |
<!-- BENCH:render-kernel end -->

These absolute times are roughly double what this table used to show (13.4 / 5.7 / 3.1 ms),
and the reason is the methodology, not a regression. Those figures were the **minimum of 25
in-process passes** — the warmest, best-cached pass of twenty-five. These are medians of a
single pass per process. The *ratios* barely moved (2.3× → 2.17×, 4.3× → 3.91×), which is
the point: a best-of-N minimum flatters the absolute number while leaving the comparison
roughly intact, and it hides that the CPython side swings ±1.5 ms run to run.

Note the **honest asterisk** on the single-threaded row: this is the *floor*, not the ceiling.
Almost all of that time is inside the XML parser + tree walk, and CPython gets to use a **native
C** parser (`expat`), so there is no interpreter loop for cheatah to leave behind here — yet the
**from-scratch cheatah parser + compiled render still finishes in ~40% of the time**, with no C
extension involved. The 62–87× gaps above appear precisely where the work *is* an interpreter loop;
a task dominated by an already-native library narrows honestly to ~2×.

### The parallel row — `memory` + `thread`, four modules cooperating

The parallel generator is the same work spread across four threads, and it is the clearest
demonstration of what the `memory` and `thread` modules are *for*: the read-only XML sources live in
<b>one pinned `memory.Owner`</b> that every thread reads through **coexisting shared read leases**, while
two accumulators (rendered bytes, and — counted with <b>`regex`</b> — the number of function members)
are `memory.Owner`s written through **exclusive write leases**. The ownership engine's
*drain-before-write* discipline makes both totals **exact** — the parallel run's output is
**byte-identical** to the single-threaded run (135 535 bytes, 585 functions, verified this run)
— so this is genuine parallelism with a **deterministic** result, not a race. At **1.79×** the
single-threaded speed (and 3.91× CPython) on a workload this small, it is the "own an object,
hand leases to threads, get honest
speedups on deterministic work" story end-to-end, in pure cheatah (`parsers.xml` + `regex` + `memory`
+ `thread`).

## Measuring `@perf`: cheatah vs CPython, exactly {#measuring-perf}

Many standard-library functions carry a **Performance** row on their reference page —
*"how much faster is this when called from cheatah than from Python?"*. Those numbers
are **not hand-written**: a periodic suite
([`scripts/perf_suite.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/perf_suite.py))
times every function and writes one provenance-tagged file (`docs/perf_data.json`,
stamped with machine, cheatah commit, CPython version, and date) that the docs render
from. It's **not** in the QA gate — benchmarks are slow, noisy, and machine-specific —
so it's run deliberately on a reference machine when the language or CPython changes,
and the regenerated file committed. Here is **exactly** how each number is produced.

The comparison is the most honest one we can make: a **compiled cheatah program**
against an **interpreted CPython program** running the *equivalent logic* — real
whole-program execution, not a micro-measurement tuned to flatter either side. (We do
**not** use `timeit`'s overhead-subtraction or best-call isolation — that measures
something other than "run this program.") Both sides <b>call the function `N` times in a
tight loop bracketed by a monotonic clock</b>, in a fresh process. cheatah is built once at
`-O3 -march=native`; CPython runs the stock interpreter (no JIT).

**How the two sides are scheduled, and what statistic is reported.** The suite runs
**7 rounds**, and each round runs the cheatah process and the CPython process
**adjacently** before either is repeated. That matters for the same reason it matters
everywhere else on this page: timing all of one side and then all of the other lets any
drift over the window settle onto one side of the ratio. Each round therefore yields one
**paired** ratio, and the published speedup is the **median of those ratios** — not the
ratio of two independently-taken medians, which the two only agree on when the machine
holds perfectly still. Each side additionally records its own median and **IQR**, so a
`@perf` row carries how *stable* the measurement was and not only how fast it was.

This replaced a best-of-N minimum taken separately per side. A minimum has no dispersion —
a rock-steady function and one that swung 40% between runs printed identical rows — and
pairing cheatah's luckiest run against CPython's luckiest is not a measurement of anything
in particular. Expect some `@perf` numbers to be *lower* than they once were for that
reason; that is the estimator being honest, not the code getting slower.

- **cheatah side.** We generate a small `.purr` that calls the function `N` times in
  a `for` loop between two `time.monotonic()` reads, compile it with `purrc` into a
  native `.so`, and run it on the `cheatah` host. So we are timing **the real
  compiled program** — the exact code path your own program would take — not a
  hand-written C++ microbenchmark.

- **CPython side** *(only when an honest one-to-one equivalent exists)*. We run the
  equivalent CPython call `N` times in a `for` loop, bracketed by `time.monotonic()`,
  in a fresh interpreter (e.g. cheatah `string.upper(s)` ↔ Python `s.upper()`,
  `math.sqrt(x)` ↔ `math.sqrt(x)`, `len(s)` ↔ `len(s)`).

The `@perf` row then reads one of two ways:

> **Performance:** `0.9 ns/call` in cheatah · `53 ns/call` in CPython 3.x · **≈59× faster**
>
> **Performance:** `0.9 ns/call` in cheatah · *(no direct CPython equivalent)*

**What the speedup measures.** Overwhelmingly, the elimination of CPython's
**per-iteration bytecode-interpretation + call overhead** — a compiled tight loop
versus an interpreted one, exactly the cost cheatah exists to remove. So the gap is
**honest about its size**: when the Python equivalent's real work already lives in C
(`hashlib.sha256`, `math.sqrt`), both sides are fast and the speedup *narrows*; where
the equivalent is itself Python-level work, it widens. We report whatever the
measurement says; we don't curate for big numbers.

A note on the optimizer: because cheatah is **compiled**, dead work simply
disappears. A loop whose result is never used is deleted outright (the interpreter is
forced to run it every iteration); a constant call is computed once; a loop-invariant
call is hoisted out. That is a real advantage — but it also means a naive
"call `f(x)` in a loop" benchmark can measure *nothing* and report a meaningless
"∞× faster". The harness above is deliberately **elision-proof** — it varies the
input each iteration, accumulates the results, and prints the total — so the numbers
reflect work that actually happened.

**Honest caveats** (so the numbers stay trustworthy):
- The figures are **machine- and CPython-version-specific** — treat them as
  *representative* and benchmark your own hardware for absolutes (same disclaimer as
  `@complexity`).
- <b>`@perf` compares against pure-CPython work, not C extensions.</b> Against a C-backed
  equivalent (`hashlib.sha256`, `math.sqrt`) the gap narrows to ~1×, reported honestly.
  For the numpy comparison, see below.

## Numerics vs NumPy and Eigen {#vs-numpy}

The numeric core is measured against two references: **NumPy** (whose array ops dispatch
to hand-tuned, often multi-threaded **BLAS/LAPACK**) and, as a single-thread C++
apples-to-apples baseline, **Eigen 3.4**. We run both honestly — same fixed-seed input,
same op many times, answers cross-checked.

- **vs NumPy**, across dense linear algebra at small-to-moderate `n` (the regime most
  scientific code runs in), cheatah **matches or beats** it on most routines (no
  Python/dispatch overhead to pay), and stays **within a small constant factor (≈1.1×)**
  on the few where NumPy's tuned BLAS/SIMD loops edge ahead — chiefly the SVD-derived
  threshold queries (`cond`, `matrix_rank`, `svdvals`). The element-wise array ops are
  competitive too: bandwidth-bound ones (`ndarray.add`, large `ndarray.sqrt`) match NumPy
  by allocating their result buffer uninitialized, and the transcendental ufuncs
  (`exp`/`sin`) win **≈4–7×** through libmvec.
- **vs Eigen on one core**, cheatah matches or beats it on the bulk (`inv`, `solve`,
  `matmul`, `dot`, `trace`, `norm`, `outer`), with Eigen leading only on its blocked
  **BLAS-3** kernels (`qr`, `cholesky`).

The full op-by-op table — methodology, the exact NumPy/Eigen versions, and the µs-per-op
numbers — lives on the [linalg reference](@ref cheatah::linalg), beside the functions it
measures (and the element-wise array math on the [ndarray reference](@ref cheatah::ndarray));
each linalg **Performance** row carries its own vs-NumPy number, and the reference table
its **vs-Eigen** column.

And cheatah does it on **one core, by design.** Its linear algebra is deliberately
**single-threaded** — a feature, not a shortfall: no hidden worker threads, no surprise
contention, no thread count to tune; the same code runs the same way every time. NumPy's
one remaining edge is the very large dense problems where its BLAS spreads across many
cores — a *different operating point*, not a faster algorithm. Our bet is the opposite:
make a single core as fast as it can possibly be, and let *you* decide when to multiply
that by your core count. Predictable single-core speed composes; opaque auto-parallelism
doesn't. And composing it is now a language feature, not a shell trick — measured next.

## Small fixed-size math vs GLM {#vs-glm}

The `NDArray` above is shape-generic and heap-backed — the right tool when the shape is
data. When the shape is a *fact about the program* — a 3-D direction, a 4×4 transform — the
generality is pure overhead: an allocation and an indirection to move sixteen floats.
[`cheatah::fixarray::Fixed`](@ref cheatah::fixarray::Fixed) is the same mathematics with the
extents moved into the type, so nothing allocates and the loops have compile-time trip
counts. Its natural comparison is not NumPy but **[GLM](https://github.com/g-truc/glm)**,
the C++ library the graphics world reaches for.

We measure the **complete overlap of the two APIs** — 160 pairs: every vector and matrix
operation (arithmetic, `dot`, `cross`, `length`, `normalize`, `distance`, `reflect`,
`min`/`max`/`clamp`, `mix`, `step`/`smoothstep`, `matmul`, `transpose`, `determinant`,
`inverse`, `matrixCompMult`, `outerProduct`, `inverseTranspose`), at sizes 2/3/4, in both
`float` and `double`. Both sides compile in one translation unit with the same flags, so
neither is handed an instruction set the other lacks, and the benchmark **verifies the two
produce identical results before it times either** — a fast wrong answer cannot pose as a
win.

<b>`Fixed` is faster than or at parity with GLM on every one — 19 faster, 141 at parity,
none slower.</b> It wins where structure pays:

<!-- BENCH:fixarray-vs-glm-highlights begin -->
<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm-highlights
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         pop-os, 20 CPUs @ 4600 MHz
     cpu-scaling:  enabled
     build:        Clang 18.1.3 (1ubuntu1), Google Benchmark v1.9.5
     competitors:  Eigen 3.4.0, GLM GLM: version 0.9.9.8, OpenSSL 3.0.13 30 Jan 2024
     harness:      reps=9, min_time=0.3s, random-interleaving=on
     statistic:    median real time per case; spread = IQR over
                   repetitions, or `sd` where
                   --benchmark_report_aggregates_only hid the raw runs
     publishable:  true
     layout:       highlights
     watch:        stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='fixarray-vs-glm-highlights' \
           CHEATAH_BENCH_LAYOUT='highlights' \
           CHEATAH_BENCH_ROWS='BM_identity_mat4f=mat4f::identity();BM_matmul_mat4f=mat4f * mat4f;BM_add_mat4f=mat4f + mat4f;BM_inverse_mat4d=inverse(mat4d);BM_abs_vec4f=abs(vec4f);BM_dot_vec4f=dot(vec4f, vec4f)' \
           CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=^BM_(identity_mat4f|matmul_mat4f|add_mat4f|inverse_mat4d|abs_vec4f|dot_vec4f)_(fixed|glm)$ --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/fixarray-vs-glm-highlights.json --benchmark_format=console
-->

| operation | `Fixed` | GLM | | |
|-----------|--------:|----:|---|---|
| `mat4f::identity()` | **0.68 ns** ±0.01 | 1.79 ns ±0.02 | 2.63× | faster |
| `mat4f * mat4f` | **3.39 ns** ±0.09 | 5.80 ns ±0.10 | 1.71× | faster |
| `mat4f + mat4f` | **0.70 ns** ±0.16 | 1.38 ns ±0.22 | 1.99× | faster |
| `inverse(mat4d)` | **12.37 ns** ±0.17 | 17.12 ns ±0.36 | 1.38× | faster |
| `abs(vec4f)` | **0.45 ns** ±0.00 | 0.83 ns ±0.02 | 1.85× | faster |
| `dot(vec4f, vec4f)` | 0.87 ns ±0.01 | 1.02 ns ±0.01 | 1.17× | parity — gap 0.15 ns |
<!-- BENCH:fixarray-vs-glm-highlights end -->

The last two rows are the interesting ones, and the reason this tally moved. `abs(vec4f)`
and `dot(vec4f)` are *nominally* ahead — 1.30× and 1.17× — but both differences are smaller
than a quarter-nanosecond, roughly one cycle. This page refuses to call anything a win
unless it clears **both** a 1.15× ratio and a 0.25 ns absolute gap, because at that scale the
harness's own `DoNotOptimize` scaffolding is a larger effect than the code. Under the earlier
methodology — best-of-N minimum, each library's cases timed in one consecutive block — the
count read **37 faster / 123 parity**. Re-measured with interleaved repetitions and medians,
it is **19 faster / 141 parity**. Eighteen operations moved from "faster" to "parity"; none
moved to "slower", and the claim that matters — that `Fixed` is never behind GLM — is
unchanged.

No intrinsics earn that — the code is *shaped* so the compiler vectorizes it, the same
promise the numeric core makes throughout. A matrix is stored **column-major**, so `m · v` is a sum
of contiguous columns rather than four horizontal dot products behind a shuffle network (and
`data()` uploads straight into a GPU push constant with no transpose); `dot` sums **pairwise**
and, at width ≥ 4, packs the products into one SIMD multiply (which also lowers the rounding
error to O(log n)); `min`/`max`/`abs`/`clamp` are branchless always-writes that lower to
`minps`/`maxps`; and every elementwise op builds its result in **one pass**, never zeroing a
buffer only to overwrite it. The full op-by-op table lives on the
[fixarray reference](@ref cheatah::fixarray), and a
[hard gate](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/bench_gate.sh)
fails the build if any pair ever regresses past GLM.

## Composing parallelism with `thread` {#vs-parallel}

The `thread` module makes the "you decide when to multiply by your core count" promise
concrete: `thread.spawn(f, args...)` runs a cheatah `fn` per worker, and a shared
`memory.Owner` collects results through leases. Here is the 20-million-point trapezoid
integral from the table above, split into per-worker chunks — each worker integrates
into a **local** sum and makes **one** lease write at the end (coordinate at the edges,
never inside the hot loop):

<!-- BENCH:thread-scaling begin -->
<!-- cheatah-bench-stamp v1
     suite:        thread-scaling
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H, 20 CPUs, Linux 7.0.11-76070011-generic (governor=powersave)
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  none — cheatah against itself at 1/2/4/8 workers
     harness:      rounds=7, striated (every configuration runs once per round)
     statistic:    median wall clock; ± is the sample standard deviation
     watch:        stdlib/thread/, stdlib/memory/, scripts/bench/integral_threads.purr
     publishable:  true

     PRODUCED BY:
       purrc scripts/bench/integral_threads.purr -o /tmp/it.so --import-root scripts && cheatah /tmp/it.so docs/bench/thread-scaling.md
-->

| workers | wall time (median) | spread | speedup | integral |
|--------:|-------------------:|-------:|--------:|----------|
| 1 | 173 ms | ±1 ms | — | 0.416268 |
| 2 | 94 ms | ±5 ms | **1.84×** | 0.416268 |
| 4 | 57 ms | ±7 ms | **3.04×** | 0.416268 |
| 8 | 41 ms | ±3 ms | **4.18×** | 0.416268 |
<!-- BENCH:thread-scaling end -->

(The benchmark is [`scripts/bench/integral_threads.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/bench/integral_threads.purr).
This table previously read 173/91/58/37 ms for a **4.7×** eight-worker speedup and described
itself as the "median of repeated runs" — it was not. The harness ran each worker count
**exactly once**, in the order 1, 2, 4, 8, so there was no median to take and no way to tell
a lucky run from a real effect. It now takes 7 rounds and runs all four configurations inside
each round, so drift across the session moves every row together instead of tilting the
scaling curve. Measured that way the eight-worker speedup is **3.77×**, not 4.7×. The
**integral is bit-identical at every worker count** — chunk sums are added through exclusive
write leases, so parallelism changes the wall clock, not the answer.)

Two honest notes. First, the scaling is real but not magic: the reference machine is a
hybrid-core laptop part (6 performance + 8 efficiency cores), so beyond the P-core
budget the marginal worker is a slower core running at a lower all-core turbo — 3.77× on
8 workers is what this silicon gives *any* native code, not a cheatah tax. The widening
spread at 2 and 4 workers (±16 ms and ±12 ms against ±3 ms at 8) is the same effect seen
from the other side: those configurations sometimes land entirely on P-cores and sometimes
straddle an E-core, and which one you get is the scheduler's choice, not the program's.
Second, the
one-lease-write-per-worker shape is the intended idiom: an `Owner` lease acquired inside
a 20M-iteration loop would benchmark the lock, not the math. The
[threading contract](threading.html) spells out the model — spawn copies its arguments,
an `Owner` travels by reference, and every thread joins before `main` returns.

## Cryptography vs OpenSSL {#vs-openssl}

cheatah's TLS 1.3 stack — SHA-2, HMAC, HKDF, ChaCha20-Poly1305, AES-GCM, X25519, Ed25519,
P-256 — is written **from scratch, with no dependency on OpenSSL or any other crypto
library**. The first thing to say about it is *correctness*: every digest, MAC, and AEAD is
cross-checked **byte-for-byte against OpenSSL** over a corpus that includes all 256 byte
values (see `stdlib/tests/hashlib_openssl_test.cpp`), on top of the standard NIST/RFC
known-answer vectors. The tag checks are constant-time. So the question is never whether the
output is right — it is — only how fast.

On throughput, cheatah reaches for the *same hardware instructions* OpenSSL does — AES-NI,
PCLMULQDQ, and (next) the SHA extensions and AVX2 — selected at run time via CPUID with a
portable scalar fallback. The accelerated paths live in a dedicated header
(`stdlib/aead/aes_gcm_ni.hpp`) so the clear, readable algorithm stays visible in `aead.cpp`;
both are validated to produce bit-identical output. On a 4 KiB record
(`tests/benchmarks/crypto_openssl_bench.cpp`, release build):

<!-- BENCH:crypto-vs-openssl begin -->
<!-- cheatah-bench-stamp v1
     suite:        crypto-vs-openssl
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         pop-os, 20 CPUs @ 4600 MHz
     cpu-scaling:  enabled
     build:        Clang 18.1.3 (1ubuntu1), Google Benchmark v1.9.5
     competitors:  Eigen 3.4.0, GLM GLM: version 0.9.9.8, OpenSSL 3.0.13 30 Jan 2024
     harness:      reps=9, min_time=0.3s, random-interleaving=on
     statistic:    median real time per case; spread = IQR over
                   repetitions, or `sd` where
                   --benchmark_report_aggregates_only hid the raw runs
     publishable:  true
     layout:       throughput
     watch:        stdlib/aead/, stdlib/hashlib/, tests/benchmarks/crypto_openssl_bench.cpp

     PRODUCED BY:
       CHEATAH_BENCH_SUITE='crypto-vs-openssl' \
           CHEATAH_BENCH_LAYOUT='throughput' \
           CHEATAH_BENCH_WATCH='stdlib/aead/, stdlib/hashlib/, tests/benchmarks/crypto_openssl_bench.cpp' \
           build/release/bin/cheatah_benchmarks --benchmark_filter=Crypto --benchmark_repetitions=9 --benchmark_min_time=0.3s --benchmark_enable_random_interleaving=true --benchmark_out_format=json --benchmark_out=docs/bench/crypto-vs-openssl.json --benchmark_format=console
-->

| Primitive | cheatah | OpenSSL | gap |
|-----------|--------:|--------:|----:|
| AES-128-GCM (AES-NI + PCLMULQDQ) | **3.67 GiB/s** | 3.48 GiB/s | **parity** (1.05×) |
| SHA-512 | 0.46 GiB/s | 0.82 GiB/s | 1.79× slower |
| ChaCha20-Poly1305 | 0.41 GiB/s | 1.71 GiB/s | 4.14× slower |
| HMAC-SHA256 | 0.31 GiB/s | 1.34 GiB/s | 4.28× slower |
| SHA-256 | 0.33 GiB/s | 1.77 GiB/s | 5.37× slower |
<!-- BENCH:crypto-vs-openssl end -->

A correction worth stating plainly, because this page got it wrong in both directions. The
AES-GCM row previously read "~2.6 GiB/s vs ~3.3 GiB/s, ~1.3×" — cheatah losing — while
[`stdlib/socket/README.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/stdlib/socket/README.md)
simultaneously claimed "3.5 GiB/s, beating OpenSSL's 3.1". Neither was right. Re-measured
under interleaved repetitions, the two are at **parity**: 3.56 vs 3.41 GiB/s is a 1.04×
difference, well inside the noise band this page uses everywhere else (1.15×), so it is
reported as a tie and not as a win.

Hardware acceleration is what closed that gap, using the same techniques OpenSSL does:
AES-NI for the block cipher, an
8-wide CTR, GHASH aggregated 8 blocks per reduction with precomputed powers of H, and the
AES-CTR and GHASH passes *stitched* into one loop so the two pipelines overlap. The other
primitives are still on the portable path, and the table shows exactly what that costs:
**SHA-2 via the SHA extensions and ChaCha20/Poly1305 via AVX2 are the next accelerations**,
each behind the same runtime dispatch. Until they land, cheatah is 1.8–5.2× off OpenSSL on
those four, and the table says so.

AES-128-GCM is hardware-accelerated on **two architectures**, each behind a runtime check and a
power-on self-test: **x86/x64** (AES-NI + PCLMULQDQ, the numbers above) and **AArch64** (the
ARMv8 AES + PMULL extension — `vaeseq`/`vaesmcq` + `vmull_p64` — the same 8-wide aggregated,
stitched algorithm). Any other target runs the portable scalar reference, which is
platform-independent C++. Throughput is closing primitive by primitive — with **zero
dependencies, one toolchain, and the readable reference algorithm always one file away**.

**Where the cryptography is verified.** The byte-for-byte cross-checks (NIST/RFC known-answer
vectors, the hardware-vs-portable equivalence test, and the comparison against OpenSSL) are run
by the QA gate on **x86-64 Linux**. On **AArch64**, the NEON hardware path *and* the scalar
reference are validated by cross-compiling the crypto and running it under **QEMU aarch64
emulation** (which implements the ARM AES/PMULL instructions) over the NIST vector, a
hardware-vs-portable equivalence sweep across every block-boundary size, and a
ChaCha20-Poly1305 round-trip (`scripts/validate_aarch64_crypto.sh`, all passing). The
**power-on self-test** further means *any* machine takes its hardware path only if that path
reproduces the known-answer vector at startup, else it falls back to the scalar reference. The
one thing not yet claimed is ARM *throughput*: emulated timings are not representative, so
AArch64 performance numbers will be added once measured on real hardware (e.g. Apple Silicon).

<!-- cheatah-bench-stamp v1
     suite:        aarch64-crypto-throughput
     host:         AArch64 hardware — none available to this project
     statistic:    n/a
     publishable:  NOT-MEASURED
     note:         AArch64 CORRECTNESS is measured and gated (QEMU, NIST vectors, a
                   hardware-vs-portable equivalence sweep, and a power-on self-test that
                   refuses the hardware path unless it reproduces the known answer).
                   Only THROUGHPUT is unmeasured, because emulated timings measure the
                   emulator. .github/workflows/macos-bench.yml publishes an AArch64 trend
                   from a shared runner; it is stamped trend-only for the same reason.
                   Nothing about the AArch64 code path is removed or weakened on account of
                   this gap — the gap is in our measurement, not in the support.
-->

## Pattern matching vs std::regex, Boost, and RE2 {#vs-regex}

The `regex` module is a from-scratch, linear-time lazy DFA. The standalone benchmark project
(`stdlib/regex/bench/`) races it against **three reference engines over identical inputs** —
`std::regex`, Boost.Regex, and **Google RE2**, the engine whose lazy-DFA approach cheatah's
matcher is modeled on. Every timed case is output-verified across all four engines before
anything is measured (the benchmark aborts on any disagreement), and a differential suite
cross-checks cheatah against RE2-as-oracle on thousands of generated inputs, including
half-megabyte adversarial ones.

The suite spans compile time, boolean search, `full_match`, `find`/find-all extraction,
realistic corpora (JSON-ish records, timestamps, hex tokens, UUIDs), input-size sweeps,
match-position shapes, tiny-input latency, and — the reason linear-time engines exist —
**catastrophic-backtracking inputs at up to 16–64 MB**, where `std::regex` needs seconds at
*28 bytes* (and is unrunnable at megabyte scale) and Boost throws a "complexity exceeded"
exception rather than answer.

The tally is carried by the generated table itself rather than repeated here — restating a
count in prose is how it goes stale, and this one had already drifted from the measurement.
Most parity rows are memory-bandwidth-bound scans where no engine separates by the gate's
1.15× margin; the one Boost win is a 64-byte pure-literal compile, analysis Boost skips and
match time repays.

**cheatah now loses two cases to RE2, and the table says so.** Both are 16 MB literal scans —
`run_padded_literal_16M` (1.18×) and `sweep_16M` (1.27×) — where the work is memcmp-bound
rather than automaton-bound and RE2's memchr-style prefilter does better than ours. This page
previously claimed *zero* losses to RE2, and `RXBENCH_ASSERT=1` is wired to fail the build on
exactly this. That assertion is currently **red**, and it should stay red until either the
prefilter improves or someone decides a sub-1.3× loss on a 16 MB literal scan is an acceptable
published position. What it must not do is get quietly relaxed to make the gate green — the
gate's whole value is that it noticed. Representative rows:

<!-- BENCH:regex-representative begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-representative
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     competitors:  std::regex, Boost.Regex, Google RE2
     statistic:    median real time per case; `vs RE2` = re2/cheatah
     harness:      medians of repeated runs, random-interleaved
     watch:        stdlib/regex/, stdlib/regex/bench/rxbench.cpp
     publishable:  true

     PRODUCED BY:
       RXBENCH_REP_TABLE=docs/bench/regex-representative.md \
           RXBENCH_ROWS='pat_literal_present=`status=200` on a 4 MB log;pat_digits=`[0-9]+` (search);pat_anchor_end=`1274$` (end-anchored, 4 MB);findall_digits=find-all `[0-9]+` (256 KB);hugescan=64 MB absent-pattern scan;compile_class_email=compile `[a-z]+@[a-z.]+`;redos2_alt2_N28=**ReDoS** `(a|aa)+$`, N=28;xl_redos_altstar_16M=**ReDoS at 16 MB** `(a|a)*c`' \
           ./build/regexbench/rxbench --benchmark_repetitions=7 \
           --benchmark_enable_random_interleaving=true
-->

| case | cheatah | std::regex | Boost | RE2 | vs RE2 |
|---|--:|--:|--:|--:|--:|
| `status=200` on a 4 MB log |      27.4 ns |     686.0 ns |      96.3 ns |      46.6 ns | 1.7× |
| `[0-9]+` (search) |       6.8 ns |      74.2 ns |      44.7 ns |      29.2 ns | 4.3× |
| `1274$` (end-anchored, 4 MB) |       5.9 ns |    35.913 ms |     107.1 ns |      30.0 ns | 5.1× |
| find-all `[0-9]+` (256 KB) |    419.80 us |     3.599 ms |     3.556 ms |     1.781 ms | 4.2× |
| 64 MB absent-pattern scan |     3.427 ms |   530.583 ms |    34.977 ms |     5.203 ms | 1.5× |
| compile `[a-z]+@[a-z.]+` |     230.6 ns |     22.87 us |     986.1 ns |      1.93 us | 8.4× |
| **ReDoS** `(a|aa)+$`, N=28 |       3.4 ns | — | — |      28.7 ns | 8.4× |
| **ReDoS at 16 MB** `(a|a)*c` |     3.863 ms | — | — |    19.462 ms | 5.0× |
<!-- BENCH:regex-representative end -->

The complete per-case table (all four engines, every benchmarked case) lives in
[the regex module's README](../stdlib/regex/README.md); regenerate it any time with
`RXBENCH_TABLE=<path> ./build/regexbench/rxbench`, and the standing claim is machine-checked —
`RXBENCH_ASSERT=1` makes the run exit non-zero if any case falls behind RE2. As everywhere on
this page: we report whatever the measurement says, losses included.

## No garbage collector — and so, no GC pauses {#no-gc}

A big part of why those loops stay fast and *predictable*: **cheatah has no garbage
collector.** There is no tracing collector, no allocation that silently arms a future
"stop the world" pause, no `gc` module to tune. All memory safety comes from two
classic, deterministic mechanisms:

- **Scopes (RAII).** Values live in local variables and STL containers and are freed
  the instant they go out of scope — the same value-semantics, stack-discipline model
  as hand-written modern C++. Most allocation in a hot loop is freed deterministically
  at the brace.
- **Reference counting** (`shared_ptr`) for the few things that are genuinely shared —
  e.g. an `ndarray`'s element buffer, which multiple views share — freed the moment the
  last reference drops.

CPython *also* reference-counts, but it adds a **cyclic garbage collector** on top to
break reference cycles, and that collector periodically walks the heap and pauses your
program. cheatah's design sidesteps it entirely: no cycles to collect (no
runtime-mutable object graph of that kind), so no collector, so **the cost of a GC pause
is exactly zero** — which is precisely what you want in a tight numeric loop, a
real-time control step, or a latency-sensitive request handler.

## How the generated code gets this fast

This page is the *comparison* — cheatah against CPython, NumPy/Eigen, and OpenSSL. *How*
`purrc` reaches these numbers is its own page: dead-variable elimination, in-place string
building (no accidental O(n²)), no-copy `auto&&` references, `match`→jump-table, and
zero-cost generic + SIMD numerics — each shown as the <b>real `.gen.cpp` purrc emits</b>, with
the un-optimized version beside it. See **[Optimizations](optimizations.html)**.

## Dynamism without the interpreter: the cheatah runtime

The usual objection to "just compile it" is that you give up what interpreters are
*good* at: loading code at runtime, hot-reloading a module without restarting, plugins,
embedding a scripting layer in a host. cheatah keeps all of it — through the
**runtime**, not an interpreter. A `.purr` program compiles not to a standalone
executable but to a **loadable module** (a `.so` exporting `extern "C" void
purr_main()`); the `cheatah` host `dlopen`s it, resolves `purr_main`, and calls it.
That `dlopen` plug-in model **is** the dynamic-loading mechanism interpreters use — but
the loaded code is **compiled native**, so it runs at full speed.

So the runtime is how cheatah serves applications that would otherwise *demand* an
interpreted language:
- **Hot reload / live update.** A long-running host — a server, a simulation, the
  plotting engine — can `dlopen` a freshly-compiled module to swap behavior without
  restarting, exactly like re-importing a Python module, but native.
- **Plug-in architectures.** An application loads user-supplied cheatah modules as
  plug-ins; the host stays small and the capabilities arrive as separately-compiled
  `.so`s it loads on demand.
- **Embeddable scripting.** Embed the `cheatah` runtime in a host app and let users
  *script it in cheatah*; their scripts compile to native modules the host loads —
  scripting-language extensibility at compiled speed.
- **A path to interactivity.** A REPL / `eval` is just this primitive in a loop:
  compile a snippet with `purrc`, `dlopen` it, run it.

The trade-off is the one this whole page is about: a **compile step** (`purrc` → `.so`)
before a module loads, so it's not type-and-eval-instantly. But you get the dynamic
loading, hot reload, plug-ins, and embedding of an interpreted runtime — *compile once
to a module, then load / run / reload it dynamically* — while every line that executes
is optimized native code.

## The standing rule

When we add a feature, we ask up front: *does this allocate or copy more than the
hand-written C++ would?* If yes, we fix it in the codegen or the library before it
ships — as with the self-append rewrite — rather than leaving it for a user to
discover with a profiler. Performance is a feature, designed in, not bolted on.
