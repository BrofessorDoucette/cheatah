# Performance {#performance}

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

cheatah exists to run **Python-shaped code at hand-written-C++ speed** — a design
constraint we apply *a priori*, before writing a feature, not an afterthought to
profile toward later. This page records how we keep cheatah fast, and the one
deliberate price we pay: **slower compilation**.

## The reference machine {#reference-machine}

Every wall-clock number on this page is **machine-specific**, so here is the machine.

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
how noisy a case was. In the Google Benchmark suites a difference is only called a win or a
loss when it clears **both** a 1.15× ratio and a 0.25 ns absolute gap; below that it is
reported as parity, because on sub-nanosecond operations the harness's own scaffolding is a
larger effect than the code. The cheatah-vs-Python and vs-NumPy suites print the paired
ratio with its round-to-round band, so a row inside that band is a tie however its winner
column reads.

Generated tables carry a `cheatah-bench-stamp` comment recording the date, commit, host,
compiler, competitor versions, repetition count and statistic used. Every published table
carries one; the QA gate's table lint rejects a benchmark table without it.

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

Measured by [`scripts/app_compare.purr`](../scripts/app_compare.purr); reproduce with `bash scripts/bench/build-harness.sh scripts/app_compare.purr /tmp/ac.so && cheatah /tmp/ac.so docs/bench/whole-programs.md`.

<!-- BENCH:whole-programs begin -->
<!-- cheatah-bench-stamp v1
     suite:        whole-programs
     generated:    2026-08-20
     commit:       f78c5e8
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
| **Mandelbrot** | escape-time over an 800×600 grid (≤256 iters) | 64 ms | 4597 ms | **72×** [69–88] |
| **Numerical integral** | trapezoid ∫ sin(x)·e^(−x/100), 20M points | 193 ms | 2773 ms | **14×** [14–26] |
| **RK4 ODE** | 4th-order Runge–Kutta, 4 000 000 steps | 44 ms | 2852 ms | **65×** [55–67] |
| **N-body** | direct O(N²) gravity, 256 bodies × 200 leapfrog steps | 36 ms | 3069 ms | **86×** [69–101] |
<!-- BENCH:whole-programs end -->

Read the brackets, not just the bold number. The N-body ratio spans 69× to 101× across
rounds because CPython's own run-to-run variance on this workload is large; a single
headline figure would hide that.

These aren't cherry-picked kernels handed to a C extension — they are *the program*,
loops and all, the part CPython interprets one bytecode at a time. The integral case is
"lowest" at 14× only because most of its time is inside `libm`'s `sin`/`exp` (native on
both sides); the pure-Python-logic programs land at **65–86×** by median. (A *chaotic* system like
Lorenz runs just as fast, but its final state diverges between floating-point
implementations — `-march=native` uses FMA, CPython doesn't — so we integrate a harmonic
oscillator and cross-check its conserved **energy** instead.)

## Dogfooding: cheatah generates its own docs {#vs-cpython-docgen}

A different shape of workload — real tooling, not a compute kernel. This very site is
**generated by a cheatah program**, [`gen.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen-cheatah/gen.purr).
Its Python ancestor, [`generate.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/docs/gen/generate.py),
renders a different site — no package switcher, no Biome Standard page, no extension
subsites — so there is no whole-site comparison: two programs doing different work cannot
be timed against each other. The comparison that stands is the render kernel below, where
`gen_bench.purr` and `gen_bench.py` do the same work over the same inputs and each prints
its byte total.

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

Measured by [`docs/gen-cheatah/gen_bench_compare.purr`](../docs/gen-cheatah/gen_bench_compare.purr); reproduce with `bash scripts/bench/build-harness.sh docs/gen-cheatah/gen_bench_compare.purr /tmp/gbc.so && cheatah /tmp/gbc.so docs/bench/render-kernel.md`.

<!-- BENCH:render-kernel begin -->
<!-- cheatah-bench-stamp v1
     suite:        render-kernel
     generated:    2026-08-20
     commit:       2b3a0b8
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
| CPython (`xml.etree`, native `expat`) | 23.8 ms | ±2.9 ms | 1.0× |
| cheatah, single-threaded (`gen_bench.purr`) | 12.2 ms | ±1.9 ms | **2.03×** |
| cheatah, 4 threads over a shared `memory.Owner` (`gen_bench_parallel.purr`) | 6.2 ms | ±0.4 ms | **3.86×** |
<!-- BENCH:render-kernel end -->

Note the **honest asterisk** on the single-threaded row: this is the *floor*, not the ceiling.
Almost all of that time is inside the XML parser + tree walk, and CPython gets to use a **native
C** parser (`expat`), so there is no interpreter loop for cheatah to leave behind here — yet the
**from-scratch cheatah parser + compiled render still finishes in about half the time**, with no C
extension involved. The 65–86× gaps above appear precisely where the work *is* an interpreter loop;
a task dominated by an already-native library narrows honestly to ~2×.

### The parallel row — `memory` + `thread`, four modules cooperating

The parallel generator is the same work spread across four threads, and it is the clearest
demonstration of what the `memory` and `thread` modules are *for*: the read-only XML sources live in
<b>one pinned `memory.Owner`</b> that every thread reads through **coexisting shared read leases**, while
two accumulators (rendered bytes, and — counted with <b>`regex`</b> — the number of function members)
are `memory.Owner`s written through **exclusive write leases**. The ownership engine's
*drain-before-write* discipline makes both totals **exact** — the parallel run's byte total and
function count match the single-threaded run — so this is genuine parallelism with a
**deterministic** result, not a race. At close to twice the single-threaded speed (3.86×
CPython) on a workload this small, it is the "own an object, hand leases to threads, get honest
speedups on deterministic work" story end-to-end, in pure cheatah (`parsers.xml` + `regex` + `memory`
+ `thread`).

## Measuring the Performance row: cheatah vs CPython, exactly {#measuring-perf}

Many standard-library functions carry a **Performance** row on their reference page —
*"how much faster is this when called from cheatah than from Python?"*. Those numbers are
**not hand-written**: a periodic suite
([`scripts/perf_suite.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/perf_suite.py))
times a fixed roster of functions and writes one provenance-tagged file
(`docs/perf_data.json`, stamped with machine, cheatah commit, CPython version and date)
that the docs render from. It is deliberately **not** in the QA gate — benchmarks are slow,
noisy and machine-specific — so it runs on the reference machine when the language or
CPython changes, and the regenerated file is committed.

The comparison is a **compiled cheatah program** against an **interpreted CPython program**
running the equivalent logic: both call the function `N` times in a tight loop bracketed by
a monotonic clock, in a fresh process. cheatah is built at `-O3 -march=native`; CPython is
the stock interpreter, no JIT. There is no `timeit` overhead-subtraction or best-call
isolation — that measures something other than running the program.

**Scheduling and statistic.** The suite runs **7 rounds**, and each round runs the cheatah
process and the CPython process **adjacently** before either repeats, so drift over the
window cannot settle onto one side of the ratio. Each round yields one **paired** ratio and
the published speedup is the **median of those ratios**, not the ratio of two independently
taken medians. Both sides also record their own median and **IQR**, so how stable a number
is stays on file alongside how big it is — a best-of-N minimum would report no dispersion at
all, printing identical rows for a rock-steady function and one that swung 40%.

- **cheatah side.** A generated `.purr` calls the function `N` times in a `for` loop between
  two `time.monotonic()` reads, compiled by `purrc` and run on the `cheatah` host — the real
  compiled program, not a hand-written C++ microbenchmark.
- **CPython side** *(only where an honest one-to-one equivalent exists)*: the same loop in a
  fresh interpreter — cheatah `string.upper(s)` ↔ Python `s.upper()`, `math.sqrt(x)` ↔
  `math.sqrt(x)`.

The row then reads (this is `math.isinf`'s):

> **Performance:** `0.90 ns/call` in cheatah · `60 ns/call` in CPython 3.12.3 · **≈66.9× faster**

Numeric routines point at their module's vs-NumPy table instead, and header-inlined builtins
such as `len` carry a one-line note in place of a number.

**What the speedup measures** is overwhelmingly the elimination of CPython's per-iteration
bytecode-interpretation and call overhead — a compiled tight loop against an interpreted one.
That makes the gap honest about its size: where the Python equivalent's real work already
lives in C (`hashlib.sha256`, `string.split`) both sides are fast and the ratio narrows
toward 1×; where the equivalent is itself Python-level work, it widens. The figures are
machine- and CPython-version-specific — representative, not absolutes for your hardware.

Because cheatah is compiled, dead work disappears: an unused result is deleted outright, a
constant call computed once, a loop-invariant call hoisted. That is a real advantage, and it
also means a naive "call `f(x)` in a loop" benchmark can measure nothing and report a
meaningless "∞× faster". The harness is therefore **elision-proof** — it varies the input
each iteration, accumulates the results and prints the total.

## Numerics vs NumPy and Eigen {#vs-numpy}

The numeric core is measured against two references: **NumPy** (whose array ops dispatch
to hand-tuned, often multi-threaded **BLAS/LAPACK**) and, as a single-thread C++
apples-to-apples baseline, **Eigen 3.4**. We run both honestly — same fixed-seed input,
same op many times, answers cross-checked.

- **vs NumPy**, across dense linear algebra at small-to-moderate `n` (the regime most
  scientific code runs in), cheatah **matches or beats** it on most routines (no
  Python/dispatch overhead to pay), and stays **within a small constant factor (≤1.2×)**
  on the few where NumPy's tuned BLAS/SIMD loops edge ahead — the SVD-derived
  threshold queries (`cond`, `matrix_rank`, `svdvals`). The element-wise array ops are
  competitive too: bandwidth-bound ones (`ndarray.add`, large `ndarray.sqrt`) match NumPy
  by allocating their result buffer uninitialized, and the transcendental ufuncs
  (`exp`/`sin`) win several-fold through libmvec — the
  [ndarray benchmarks](../stdlib/ndarray/BENCHMARKS.md) page has the rows.
- **vs Eigen on one core**, the eight-case table has cheatah ahead on `inv`, `solve`,
  `matmul` and the long `dot`, at parity on the 96×96 `matmul`, and behind only on the
  64-element `dot`.

The full op-by-op table — methodology, the exact NumPy/Eigen versions, and the µs-per-op
numbers — lives on the [linalg benchmarks](../stdlib/linalg/BENCHMARKS.md) page (and the
element-wise array math on the [ndarray benchmarks](../stdlib/ndarray/BENCHMARKS.md) page);
each linalg **Performance** row on the [linalg reference](@ref cheatah::linalg) carries its
own vs-NumPy number, and the benchmarks page its **vs-Eigen** table.

And cheatah does it on **one core, by design.** Its linear algebra is deliberately
**single-threaded** — a feature, not a shortfall: no hidden worker threads, no surprise
contention, no thread count to tune; the same code runs the same way every time. NumPy's
one remaining edge is the very large dense problems where its BLAS spreads across many
cores — a *different operating point*, not a faster algorithm. Our bet is the opposite:
make a single core as fast as it can possibly be, and let *you* decide when to multiply
that by your core count. Predictable single-core speed composes; opaque auto-parallelism
doesn't. Composing it is a language feature, not a shell trick — see @ref vs-parallel.

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

<b>`Fixed` is faster than or at parity with GLM on all 160 operations and slower on
none</b> — the [full table](../stdlib/fixarray/BENCHMARKS.md) carries the tally. It wins
where structure pays:

Measured by [`tests/benchmarks/fixed_glm_bench.cpp`](../tests/benchmarks/fixed_glm_bench.cpp); reproduce with `scripts/bench_run.sh publish fixarray-vs-glm-highlights`.

<!-- BENCH:fixarray-vs-glm-highlights begin -->
<!-- cheatah-bench-stamp v1
     suite:        fixarray-vs-glm-highlights
     generated:    2026-08-20
     commit:       2b3a0b8
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
| `mat4f::identity()` | **0.66 ns** ±0.01 | 1.78 ns ±0.05 | 2.69× | faster |
| `mat4f * mat4f` | **3.38 ns** ±0.04 | 5.75 ns ±0.14 | 1.70× | faster |
| `mat4f + mat4f` | **0.67 ns** ±0.01 | 1.36 ns ±0.06 | 2.03× | faster |
| `inverse(mat4d)` | **12.15 ns** ±0.31 | 16.83 ns ±0.36 | 1.38× | faster |
| `abs(vec4f)` | **0.44 ns** ±0.01 | 0.82 ns ±0.01 | 1.85× | faster |
| `dot(vec4f, vec4f)` | 0.85 ns ±0.02 | 0.99 ns ±0.02 | 1.17× | parity — gap 0.14 ns |
<!-- BENCH:fixarray-vs-glm-highlights end -->

The last row is the interesting one. `dot(vec4f)` is *nominally* ahead at 1.17×, but the
difference is 0.14 ns, roughly one cycle, so the table calls it parity. This page refuses to
call anything a win unless it clears **both** a 1.15× ratio and a 0.25 ns absolute gap,
because at that scale the harness's own `DoNotOptimize` scaffolding is a larger effect than
the code. The claim that matters — that `Fixed` is never behind GLM — holds across all 160
operations.

No intrinsics earn that — the code is *shaped* so the compiler vectorizes it, the same
promise the numeric core makes throughout. A matrix is stored **column-major**, so `m · v` is a sum
of contiguous columns rather than four horizontal dot products behind a shuffle network (and
`data()` uploads straight into a GPU push constant with no transpose); `dot` sums **pairwise**
and, at width ≥ 4, packs the products into one SIMD multiply (which also lowers the rounding
error to O(log n)); `min`/`max`/`abs`/`clamp` are branchless always-writes that lower to
`minps`/`maxps`; and every elementwise op builds its result in **one pass**, never zeroing a
buffer only to overwrite it. The full op-by-op table lives on the
[fixarray benchmarks](../stdlib/fixarray/BENCHMARKS.md) page, and a
[hard gate](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/bench_gate.sh)
fails the build if any pair ever regresses past GLM.

## Composing parallelism with `thread` {#vs-parallel}

The `thread` module makes the "you decide when to multiply by your core count" promise
concrete: `thread.spawn(f, args...)` runs a cheatah `fn` per worker, and a shared
`memory.Owner` collects results through leases. Here is the 20-million-point trapezoid
integral from the table above, split into per-worker chunks — each worker integrates
into a **local** sum and makes **one** lease write at the end (coordinate at the edges,
never inside the hot loop):

Measured by [`scripts/bench/integral_threads.purr`](../scripts/bench/integral_threads.purr); reproduce with `purrc scripts/bench/integral_threads.purr -o /tmp/it.so --import-root scripts && cheatah /tmp/it.so docs/bench/thread-scaling.md`.

<!-- BENCH:thread-scaling begin -->
<!-- cheatah-bench-stamp v1
     suite:        thread-scaling
     generated:    2026-08-20
     commit:       2b3a0b8
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
| 1 | 176 ms | ±5 ms | — | 0.416268 |
| 2 | 93 ms | ±4 ms | **1.88×** | 0.416268 |
| 4 | 56 ms | ±3 ms | **3.16×** | 0.416268 |
| 8 | 39 ms | ±3 ms | **4.46×** | 0.416268 |
<!-- BENCH:thread-scaling end -->

Seven rounds, every configuration once per round, so drift across the session moves every
row together instead of tilting the scaling curve. The integral agrees at every worker count
to every printed digit — chunk sums are added through exclusive write leases, so parallelism
changes the wall clock, not the answer.

Two honest notes. First, the scaling is real but not magic: the reference machine is a
hybrid-core laptop part (6 performance + 8 efficiency cores), so beyond the P-core
budget the marginal worker is a slower core running at a lower all-core turbo — 4.46× on
8 workers is what this silicon gives *any* native code, not a cheatah tax. Whether a 2- or
4-worker run lands entirely on P-cores or straddles an E-core is the scheduler's choice,
not the program's. Second, the one-lease-write-per-worker shape is the intended idiom: an
`Owner` lease acquired inside a 20M-iteration loop would benchmark the lock, not the math.
The [threading contract](../stdlib/thread/README.md) spells out the model — spawn copies its
arguments, an `Owner` travels by reference, and every thread joins before `main` returns.

## Cryptography vs OpenSSL {#vs-openssl}

cheatah's TLS 1.3 stack — SHA-2, HMAC, HKDF, ChaCha20-Poly1305, AES-GCM, X25519, Ed25519,
P-256 — is written **from scratch, with no dependency on OpenSSL or any other crypto
library**. The first thing to say about it is *correctness*: every digest and MAC is
cross-checked **byte-for-byte against OpenSSL** over a corpus that includes all 256 byte
values (see `stdlib/tests/hashlib_openssl_test.cpp`), and every AEAD against the standard
NIST/RFC known-answer vectors. The tag checks are constant-time. So the question is never whether the
output is right — it is — only how fast.

On throughput, cheatah reaches for the *same hardware instructions* OpenSSL does — AES-NI,
PCLMULQDQ, and (next) the SHA extensions and AVX2 — selected at run time via CPUID with a
portable scalar fallback. The accelerated paths live in a dedicated header
(`stdlib/aead/aes_gcm_ni.hpp`) so the clear, readable algorithm stays visible in `aead.cpp`;
both are validated to produce bit-identical output. On a 4 KiB record
(`tests/benchmarks/crypto_openssl_bench.cpp`, release build):

Measured by [`tests/benchmarks/crypto_openssl_bench.cpp`](../tests/benchmarks/crypto_openssl_bench.cpp); reproduce with `scripts/bench_run.sh publish crypto-vs-openssl`.

<!-- BENCH:crypto-vs-openssl begin -->
<!-- cheatah-bench-stamp v1
     suite:        crypto-vs-openssl
     generated:    2026-08-20
     commit:       2b3a0b8
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
| AES-128-GCM (AES-NI + PCLMULQDQ) | **3.66 GiB/s** | 3.45 GiB/s | **parity** (1.06×) |
| SHA-512 | 0.46 GiB/s | 0.82 GiB/s | 1.78× slower |
| ChaCha20-Poly1305 | 0.42 GiB/s | 1.70 GiB/s | 4.04× slower |
| HMAC-SHA256 | 0.32 GiB/s | 1.37 GiB/s | 4.30× slower |
| SHA-256 | 0.33 GiB/s | 1.78 GiB/s | 5.43× slower |
<!-- BENCH:crypto-vs-openssl end -->

AES-GCM is a tie, not a win: 1.06× sits well inside the noise band this page uses
everywhere else (1.15×), so the table reports parity.

Hardware acceleration is what closed that gap, using the same techniques OpenSSL does:
AES-NI for the block cipher, an
8-wide CTR, GHASH aggregated 8 blocks per reduction with precomputed powers of H, and the
AES-CTR and GHASH passes *stitched* into one loop so the two pipelines overlap. The other
primitives are still on the portable path, and the table shows exactly what that costs:
**SHA-2 via the SHA extensions and ChaCha20/Poly1305 via AVX2 are the next accelerations**,
each behind the same runtime dispatch. Until they land, cheatah is 1.8–5.4× off OpenSSL on
those four, and the table says so.

AES-128-GCM is hardware-accelerated on **two architectures**, each behind a runtime check and
a power-on self-test: **x86/x64** (AES-NI + PCLMULQDQ, the numbers above) and **AArch64**
(the ARMv8 AES + PMULL extension — `vaeseq`/`vaesmcq` + `vmull_p64` — the same 8-wide
aggregated, stitched algorithm). Any other target runs the portable scalar reference. The
self-test means *any* machine takes its hardware path only if that path reproduces the
known-answer vector at startup, else it falls back to the reference.

**Where the cryptography is verified.** The byte-for-byte cross-checks — NIST/RFC
known-answer vectors, the hardware-vs-portable equivalence sweep across every block-boundary
size, and the comparison against OpenSSL — run in the QA gate on x86-64 Linux. The AArch64
NEON path and its scalar reference are validated the same way under **QEMU** emulation
(`scripts/validate_aarch64_crypto.sh`). Only ARM *throughput* goes unclaimed: emulated
timings measure the emulator, so those numbers wait for real hardware.

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

The `regex` module is a from-scratch lazy DFA that never backtracks. The standalone benchmark project
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

The tally is carried by the generated table itself rather than repeated here — a count
restated in prose is a count that drifts.
Most parity rows are memory-bandwidth-bound scans where no engine separates by the gate's
1.15× margin; the one Boost win is a 64-byte pure-literal compile, analysis Boost skips and
match time repays.

**Two borderline rows bound what the tally means.** The 16 MB literal scans
`run_padded_literal_16M` and `sweep_16M` sit at parity with RE2 by a hair: they are
memcmp-bound rather than automaton-bound, so the comparison is really between two prefilters
doing nearly the same work, and the gap sits close enough to the 1.15× threshold that a run
can land on either side. Any thresholded count behaves that way. `RXBENCH_ASSERT=1` exits
non-zero on an RE2 loss, so it will occasionally go red on these two — the response is to read
the margin, not to widen the threshold until it stops. Representative rows:

Measured by [`stdlib/regex/bench/rxbench.cpp`](../stdlib/regex/bench/rxbench.cpp); reproduce with `RXBENCH_REP_TABLE=docs/bench/regex-representative.md ./build/regexbench/rxbench`.

<!-- BENCH:regex-representative begin -->
<!-- cheatah-bench-stamp v1
     suite:        regex-representative
     generated:    2026-08-20
     commit:       2b3a0b8
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
| `status=200` on a 4 MB log |      26.5 ns |     649.9 ns |      95.0 ns |      46.6 ns | 1.8× |
| `[0-9]+` (search) |       6.8 ns |      74.1 ns |      44.5 ns |      28.7 ns | 4.3× |
| `1274$` (end-anchored, 4 MB) |       6.0 ns |    35.713 ms |     107.9 ns |      30.0 ns | 5.0× |
| find-all `[0-9]+` (256 KB) |    428.83 us |     3.512 ms |     3.304 ms |     1.748 ms | 4.1× |
| 64 MB absent-pattern scan |     3.391 ms |   526.047 ms |    35.253 ms |     5.144 ms | 1.5× |
| compile `[a-z]+@[a-z.]+` |     218.8 ns |     22.30 us |      1.02 us |      1.96 us | 9.0× |
| **ReDoS** `(a|aa)+$`, N=28 |       3.4 ns | — | — |      28.1 ns | 8.2× |
| **ReDoS at 16 MB** `(a|a)*c` |     3.927 ms | — | — |    19.393 ms | 4.9× |
<!-- BENCH:regex-representative end -->

The complete per-case table (all four engines, every benchmarked case) lives on
[the regex benchmarks page](../stdlib/regex/BENCHMARKS.md); regenerate it any time with
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
