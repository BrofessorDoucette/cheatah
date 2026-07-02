# Performance {#performance}

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

cheatah exists to run **Python-shaped code at hand-written-C++ speed** — a design
constraint we apply *a priori*, before writing a feature, not an afterthought to
profile toward later. This page records how we keep cheatah fast, and the one
deliberate price we pay: **slower compilation**.

## The core bargain: compile-time cost for run-time speed

A `.purr` program is transpiled to modern C++ and built at **`-O3 -march=native`**,
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
times are compute-only (startup excluded), best of several runs on the reference
machine:

| program | what it does | cheatah | CPython | speedup |
|---------|--------------|--------:|--------:|--------:|
| **Mandelbrot** | escape-time over an 800×600 grid (≤256 iters) | 60 ms | 4262 ms | **71×** |
| **N-body** | direct O(N²) gravity, 256 bodies × 200 leapfrog steps | 30 ms | 2854 ms | **96×** |
| **RK4 ODE** | 4th-order Runge–Kutta, 4 000 000 steps | 40 ms | 2589 ms | **65×** |
| **Numerical integral** | trapezoid ∫ sin(x)·e^(−x/100), 20M points | 174 ms | 2504 ms | **14×** |

These aren't cherry-picked kernels handed to a C extension — they are *the program*,
loops and all, the part CPython interprets one bytecode at a time. The integral case is
"lowest" at 14× only because most of its time is inside `libm`'s `sin`/`exp` (native on
both sides); the pure-Python-logic programs land at **65–96×**. (A *chaotic* system like
Lorenz runs just as fast, but its final state diverges between floating-point
implementations — `-march=native` uses FMA, CPython doesn't — so we integrate a harmonic
oscillator and cross-check its conserved **energy** instead.)

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
something other than "run this program.") Both sides **call the function `N` times in a
tight loop bracketed by a monotonic clock**, in a fresh process, and report the per-call
time (minimum over several trials, to remove OS jitter). cheatah is built once at
`-O3 -march=native`; CPython runs the stock interpreter (no JIT).

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
- **`@perf` compares against pure-CPython work, not C extensions.** Against a C-backed
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
that by your core count. Eight cores and an embarrassingly parallel sweep — a parameter
scan, a batch of small eigenproblems, a Monte-Carlo run — give an honest ~8× by running
eight cheatah problems at once, explicitly, with no magic. Predictable single-core speed
composes; opaque auto-parallelism doesn't.

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

| Primitive | cheatah | OpenSSL | gap |
|-----------|--------:|--------:|----:|
| AES-128-GCM (AES-NI + PCLMULQDQ) | ~2.6 GiB/s | ~3.3 GiB/s | ~1.3× |
| ChaCha20-Poly1305 | ~0.44 GiB/s | ~1.74 GiB/s | ~4× |
| SHA-256 | ~330 MiB/s | ~1.77 GiB/s | ~5× |
| SHA-512 | ~473 MiB/s | ~803 MiB/s | ~1.7× |
| HMAC-SHA256 | ~318 MiB/s | ~1.22 GiB/s | ~4× |

Hardware acceleration took **AES-128-GCM from ~20 MiB/s to ~2.6 GiB/s — a 134× jump — to within
~1.3× of OpenSSL**, using the same techniques OpenSSL does: AES-NI for the block cipher, an
8-wide CTR, GHASH aggregated 8 blocks per reduction with precomputed powers of H, and the
AES-CTR and GHASH passes *stitched* into one loop so the two pipelines overlap. What's left of
the gap is OpenSSL's hand-scheduled assembly, which interleaves the AES and carry-less-multiply
instructions more tightly than a compiler does from intrinsics. The other primitives are still
on the portable path — **SHA-2 via the SHA extensions and ChaCha20/Poly1305 via AVX2 are the
next accelerations**, each behind the same runtime dispatch.

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
zero-cost generic + SIMD numerics — each shown as the **real `.gen.cpp` purrc emits**, with
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
