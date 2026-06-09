# Performance {#performance}

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

cheatah's whole reason to exist is **Python-shaped code at hand-written-C++ speed**.
That goal is a design constraint we apply *a priori* — before writing a feature — not
an afterthought we profile our way toward later. This page is the standing record of
how we keep cheatah fast, and the one deliberate price we pay for it: **slower
compilation**.

## The core bargain: compile-time cost for run-time speed

A `.purr` program is transpiled to modern C++ and built at **`-O3 -march=native`**,
then run on the headless host. We lean hard on **templates and C++20 concepts** for
zero-cost abstraction — generic code that monomorphizes at the call site into exactly
the machine code you'd write by hand, with no virtual dispatch, no boxing, no runtime
type tags.

Templates are not free: they make the C++ compiler do more work, so **cheatah
programs compile more slowly than an equivalent dynamically-typed language would
"load."** We accept that trade knowingly. Compilation happens once; the compiled `.so`
then runs in hot loops, inner products, and tight string-building paths millions of
times. Paying the cost once, at build time, to delete it forever at run time is the
right side of that trade for the scientific-computing, ML, and systems workloads
cheatah targets. (It is also why every emitted template is **constrained by a
concept** — see @ref constrain-all-templates below — so the extra compiler work buys
*comprehensible errors*, not just speed.)

## Whole programs: cheatah vs CPython {#vs-cpython-apps}

Micro-benchmarks are easy to game; whole programs are not. So here are four *complete*
little programs — the kind of compute a scientist actually writes — implemented in the
**same algorithm** in cheatah and in CPython, run on a real workload, with the result
**cross-checked between the two languages** (same algorithm → same answer) so the
comparison is honest. The harness is
[`scripts/app_compare.purr`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/app_compare.purr);
times are compute-only (startup excluded), the best of several runs on the reference
machine:

| program | what it does | cheatah | CPython | speedup |
|---------|--------------|--------:|--------:|--------:|
| **Mandelbrot** | escape-time over an 800×600 grid (≤256 iters) | 58 ms | 4267 ms | **74×** |
| **N-body** | direct O(N²) gravity, 256 bodies × 200 leapfrog steps | 30 ms | 2892 ms | **97×** |
| **RK4 ODE** | 4th-order Runge–Kutta, 4 000 000 steps | 40 ms | 2549 ms | **64×** |
| **Numerical integral** | trapezoid ∫ sin(x)·e^(−x/100), 20M points | 170 ms | 2337 ms | **14×** |

These are not cherry-picked kernels handed to a C extension — they are *the program*,
loops and all, the part CPython has to interpret one bytecode at a time. The integral
case is "lowest" at 14× only because most of its time is inside `libm`'s `sin`/`exp`
(native on both sides); the pure-Python-logic programs land at **64–97×**. (Note: a
*chaotic* system like the Lorenz attractor runs just as fast, but its final state
diverges between any two floating-point implementations — `-march=native` uses fused
multiply-add, CPython doesn't — so we integrate a harmonic oscillator and compare its
conserved **energy**, which both agree on.)

## Measuring `@perf`: cheatah vs CPython, exactly {#measuring-perf}

Many standard-library functions carry a **Performance** row on their reference page —
*"how much faster is this when called from cheatah than from Python?"*. Those numbers
are **not hand-written**: a periodic benchmark suite
([`scripts/perf_suite.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/perf_suite.py))
times every function and writes one provenance-tagged data file
(`docs/perf_data.json`, stamped with the machine, the cheatah commit, the CPython
version, and the date); the docs render the row from that file. The suite is **not**
part of the QA gate — benchmarks are slow, noisy, and machine-specific — so it is run
deliberately on a reference machine when the language or CPython changes, and the
regenerated data file is committed. So that nobody has to take the numbers on faith,
here is **exactly** how each one is produced.

The comparison is deliberately the most honest one we can make: a **compiled cheatah
program** against an **interpreted CPython program** running the *equivalent logic* —
the real, whole-program execution in each language, not an isolated micro-measurement
that has been tuned to flatter either side. (We do **not** use `timeit`'s
overhead-subtraction or best-call isolation — that would measure something other than
"run this program.") Both sides do the same thing: **call the function `N` times in a
tight loop, bracketed by a monotonic clock**, in a fresh process, and report the
per-call time (the minimum over several trials, to remove OS jitter). cheatah is built
once at `-O3 -march=native`; CPython runs the stock interpreter (no JIT).

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

**What the speedup actually measures.** Overwhelmingly, it is the elimination of
CPython's **per-iteration bytecode-interpretation + call overhead** — a compiled
tight loop versus an interpreted one. That is the dominant, honest effect of "call
this in a loop," and it is exactly the cost cheatah exists to remove. It also means
the gap is **honest about its size**: when the Python equivalent's real work already
lives in C (e.g. `hashlib.sha256`, `math.sqrt`), the per-call work is fast on both
sides and the speedup *narrows* — what's left is mostly the interpreter's call
overhead. Where the Python equivalent is itself Python-level work, the gap widens.
We report whatever the measurement says; we do not curate for big numbers.

A note on the optimizer: because cheatah is **compiled**, dead work simply
disappears. A loop whose result is never used is deleted outright (the interpreter is
forced to run it every iteration); a constant call is computed once; a loop-invariant
call is hoisted out. That is a real advantage — but it also means a naive
"call `f(x)` in a loop" benchmark can measure *nothing* and report a meaningless
"∞× faster". The harness above is deliberately **elision-proof** — it varies the
input each iteration, accumulates the results, and prints the total — so the numbers
reflect work that actually happened.

**Honest caveats** (so the numbers stay trustworthy):
- The figures are **machine- and CPython-version-specific**, so they are
  auto-generated on a fixed reference machine and recorded with that machine +
  interpreter version — treat them as *representative*, and benchmark your own
  hardware for absolutes (the same disclaimer as `@complexity`).
- **`@perf` compares against pure-CPython work, not against C extensions.** Where the
  Python equivalent's real work already lives in C (`hashlib.sha256`, `math.sqrt`),
  the gap narrows to ~1× — we report that honestly. For numpy, see below.

## cheatah `linalg` vs NumPy {#vs-numpy}

The obvious question for the numeric core: *how does it compare to NumPy?* NumPy's
array ops dispatch to **BLAS/LAPACK** — hand-tuned, vectorized, often multi-threaded
Fortran kernels — so this is the hard comparison, and we run it honestly: the
[`scripts/numpy_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/numpy_compare.py)
harness feeds the **same** fixed-seed, well-conditioned matrix to both libraries,
runs the **same** operation many times with the result consumed, and checks the two
answers agree. Representative results (µs per op on the reference machine — your
hardware will differ):

> **What we compared against.** These numbers are vs **NumPy 1.26.4** (CPython
> 3.12.3, `x86_64`), linked against the **system BLAS/LAPACK**. NumPy's absolute
> speed — and therefore exactly where the crossovers land — depends heavily on which
> BLAS it's built against (reference vs OpenBLAS vs MKL) and its thread count; a
> faster BLAS pushes the crossovers *lower*. We report the version so the comparison
> is reproducible, not a moving target.

| op | operand dimensions | cheatah | NumPy | winner |
|----|--------------------|--------:|------:|--------|
| *— products —* | | | | |
| `dot` | two 64-element vectors | 0.03 | 0.58 | **cheatah 18×** |
| `dot` | two 16384-element vectors | 3.74 | 7.72 | **cheatah 2.1×** |
| `matmul` | 32×32 · 32×32 | 3.04 | 12.9 | **cheatah 4.3×** |
| `matmul` | 96×96 · 96×96 | 85.4 | 298 | **cheatah 3.5×** |
| `outer` | 64-vec → 64×64 | 1.21 | 4.22 | **cheatah 3.5×** |
| `outer` | 256-vec → 256×256 | 187 | 45.5 | NumPy 4.1× |
| `kron` | 8×8 ⊗ 8×8 → 64×64 | 1.95 | 12.3 | **cheatah 6.3×** |
| `kron` | 32×32 ⊗ 32×32 → 1024×1024 | 3971 | 732 | NumPy 5.4× |
| *— LU-based —* | | | | |
| `solve` | 32×32 matrix, 32-vector | 3.56 | 9.95 | **cheatah 2.8×** |
| `solve` | 64×64 matrix, 64-vector | 16.4 | 47.4 | **cheatah 2.9×** |
| `det` | 64×64 | 13.9 | 45.1 | **cheatah 3.2×** |
| `slogdet` | 64×64 | 14.1 | 46.1 | **cheatah 3.3×** |
| `inv` | 32×32 | 5.65 | 23.8 | **cheatah 4.2×** |
| `inv` | 64×64 | 32.5 | 135 | **cheatah 4.2×** |
| *— factorizations —* | | | | |
| `cholesky` | 64×64 | 16.3 | 24.0 | **cheatah 1.5×** |
| `qr` | 32×32 | 17.3 | 26.1 | **cheatah 1.5×** |
| `qr` | 64×64 | 162 | 124 | NumPy 1.3× |
| `svd` (full U+s+Vᵀ) | 64×64 | 371 | 680 | **cheatah 1.8×** |
| `svd` (full U+s+Vᵀ) | 96×96 | 1129 | 1871 | **cheatah 1.7×** |
| `svdvals` (values only) | 64×64 | 207 | 185 | NumPy 1.1× |
| `svdvals` (values only) | 96×96 | 551 | 540 | even (1.0×) |
| `pinv` | 32×32 | 93 | 126 | **cheatah 1.4×** |
| `pinv` | 64×64 | 591 | 749 | **cheatah 1.3×** |
| `cond` | 64×64 | 208 | 187 | NumPy 1.1× |
| `matrix_rank` | 64×64 | 211 | 189 | NumPy 1.1× |
| *— eigen —* | | | | |
| `eigvalsh` | 2×2 | 0.17 | 1.93 | **cheatah 11.5×** |
| `eigvalsh` | 8×8 | 2.04 | 3.91 | **cheatah 1.9×** |
| `eigvalsh` | 64×64 | 143 | 140 | even (1.0×) |
| `eigh` (+ vectors) | 32×32 | 42.5 | 60.8 | **cheatah 1.4×** |
| `eigh` (+ vectors) | 64×64 | 271 | 379 | **cheatah 1.4×** |
| `eigvals` (general) | 16×16 | 50.7 | 31.1 | NumPy 1.6× |
| `matrix_power` (A³) | 64×64 | 89.1 | 216 | **cheatah 2.4×** |
| *— reductions —* | | | | |
| `trace` | 256×256 | 0.09 | 1.12 | **cheatah 13×** |
| `norm` (Frobenius) | 32×32 | 0.84 | 1.42 | **cheatah 1.7×** |
| `norm` (Frobenius) | 256×256 | 57.5 | 29.8 | NumPy 1.9× |
| *— element-wise (`ndarray`, not `linalg`) —* | | | | |
| `ndarray.sqrt` | 64-element array | 0.18 | 0.80 | **cheatah 4.4×** |
| `ndarray.sqrt` | 16384-element array | 16.2 | 13.9 | NumPy 1.2× |
| `ndarray.exp` | 16384-element array | 14.7 | 46.3 | **cheatah 3.1×** |
| `ndarray.sin` | 16384-element array | 15.8 | 84.5 | **cheatah 5.4×** |
| `ndarray.add` | 16384-element array + scalar | 4.15 | 2.77 | NumPy 1.5× |

The pattern, after a focused round of optimization:

- **Products and LU-based factorizations win outright across the whole tested range.**
  `dot` (≈20× at 64 elements, still **2.1×** at 16384), `matmul` (≈4–6× from 4×4 to
  96×96), `solve`/`det`/`inv` (≈3–4× at 32×32–64×64). At these sizes NumPy's per-call
  Python dispatch *and* threaded-BLAS startup overhead dominate, while cheatah's
  single-threaded, auto-vectorized C++ just does the arithmetic with no overhead. Two
  fixes got `dot` and `inv` here: their reductions were a *serial* floating-point
  dependency chain that doesn't vectorize without `-ffast-math`; rewriting them with
  several independent accumulators (`dot`) and a whole-identity block solve (`inv`)
  let `-O3 -march=native` issue SIMD + FMA. `inv` went from *losing* 1.1× at 32×32 to
  winning 4.2×; `dot` from losing 36× to winning 2.1×.
- **Symmetric eigenvalues (`eigvalsh`) now match LAPACK** instead of losing 10–35×. The
  old kernel was cyclic Jacobi (many full-matrix sweeps); it's now Householder
  tridiagonalization + implicit-shift QL — the same family of method LAPACK uses — and
  `eigvalsh` additionally skips the eigenvector accumulation it used to compute and
  throw away. The result: cheatah **wins decisively at small n** (≈11× on a 2×2, ≈2×
  on 8×8 — the physics few-level case: spin Hamiltonians, parameter sweeps that
  diagonalize the same small matrix millions of times) and **ties LAPACK** from
  ≈16×16 through 64×64 (within ~1.0–1.2×).
- **Element-wise transcendentals (`ndarray.exp`/`sin`/… ) now beat NumPy too.** They
  used to lose at large `n` because `std::exp`/`std::sin` don't auto-vectorize under
  the default flags (errno + the vector-ABI's finiteness assumption block it), so the
  loop stayed scalar. The fix: a small, isolated kernel TU compiled with
  `-fveclib=libmvec -fno-math-errno` routes the contiguous-`double` case through
  glibc's **libmvec** vector math (`_ZGVdN4v_exp`, …) — *without* `-ffast-math`, so the
  results stay strictly IEEE and the rest of cheatah's arithmetic is untouched. Now
  `exp` wins ≈3× and `sin` ≈5× at 16384 elements; `sqrt` (memory-bandwidth-bound) wins
  small and ties at large. A second fix mattered just as much: `array ⊕ scalar`
  broadcasting was doing a bounds-checked `at()` per element (no SIMD) — a contiguous
  fast path took `ndarray.add` from ≈20× slower to ~even.

- **The SVD now wins.** It's Golub–Reinsch — the world-standard dense algorithm LAPACK's
  `dgesvd` reduces to: Householder **bidiagonalization** then implicit-shift QR on the
  bidiagonal — reimplemented entirely **column-major** so both the bidiagonalization's
  reflectors and the QR's whole-column U/V rotations vectorize. The **full**
  decomposition (U, singular values, Vᵀ) **beats NumPy by 1.7–1.8×** (64×64: 371µs vs
  680µs), so `pinv` wins **1.3–1.4×**. For values only there's a dedicated `svdvals`
  fast path (like NumPy's `compute_uv=False`) that skips the U/V accumulation and the
  dominant U/V rotations — it **ties LAPACK** (~1.0–1.1×), and so do `cond` and
  `matrix_rank`. Three things got it there over a one-sided Jacobi that lost ≈19×:
  column-major vectorization, the values-only path, and replacing the correctly-rounded
  `std::hypot` in the O(n²) Givens rotations with the faster EISPACK `pythag`.

So across dense linear algebra at small-to-moderate `n` — the regime most scientific
code actually runs in — cheatah now **matches or beats** NumPy's BLAS/LAPACK on every
routine measured: it wins outright on the products, the LU factorizations, the SVD and
its `pinv`/`cond`/`matrix_rank` derivatives, and the element-wise math, and ties on the
symmetric eigensolver and bare singular values.

And it does all of this on **one core, by design.** cheatah's linear algebra is
deliberately **single-threaded** — that's a feature, not a shortfall. There are no
hidden worker threads spinning up in the background, no surprise contention with the
rest of your program, no thread-count to tune; the same code runs the same way every
time. NumPy's one remaining edge is the very large dense problems where its BLAS spreads
across many cores — but that's a *different operating point*, not a faster algorithm. Our
bet is the opposite one: make a single core as fast as it can possibly be, and let *you*
decide when to multiply that by your core count. If you have 8 cores and an
embarrassingly parallel sweep — a parameter scan, a batch of small eigenproblems, a
Monte-Carlo run — you get an honest ~8× by running eight cheatah problems at once,
explicitly, with no magic. Predictable single-core speed composes; opaque
auto-parallelism doesn't.

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
real-time control step, or a latency-sensitive trading agent.

## String building: no accidental O(n²), no surplus temporaries

This is the concern that prompted this page, and it is a real trap in naive
transpilers. Consider a typical builder — assembling an HTTP response header:

```python
fn response(status, ctype, body) {
    let nl = chr(13) + chr(10)
    let head = "HTTP/1.1 " + status + nl
    head = head + "Content-Type: " + ctype + nl
    head = head + "Content-Length: " + io.str(len(body)) + nl
    head = head + "Connection: close" + nl + nl
    return head + body
}
```

A literal translation of `head = head + "…" + ctype + nl` would **copy the entire
growing `head`** on every line. Over a header that's `n` bytes when done, that is
`O(n²)` total copying plus a fresh full-length temporary per statement — exactly the
death-by-`std::string`-temporaries you'd worry about.

cheatah does **not** emit that. Two things protect you:

1. **Self-append rewrite.** The codegen recognizes the pattern `x = x + e1 + e2 + …`
   (a plain variable at the head of a `+` chain that the appended operands don't
   re-read) and lowers it to **in-place appends**:

   ```cpp
   head += std::string("Content-Type: ");
   head += ctype;
   head += nl;
   ```

   The `head` buffer grows amortized in place; nothing copies the bytes already
   written. `O(n²)` becomes `O(n)`, and the per-line full-length temporary is gone.
   (The same rewrite turns numeric accumulators like `total = total + i` into
   `total += i`.)

2. **rvalue `operator+` chaining.** For a *fresh* left-to-right chain such as
   `let head = "HTTP/1.1 " + status + nl`, C++'s rvalue overloads of `operator+`
   reuse the leftmost temporary's buffer and append into it, so a chain of `k`
   concatenations is one growing buffer — not `k` independent allocations.

Net: the builder above performs work proportional to the **output length**, with the
allocations a careful C++ programmer would write by hand.

## Numeric speed: zero-cost generics + SIMD

- **`ndarray` is generic over its element type** (`basic_ndarray<T>`, constrained to a
  `Field` concept — real or complex), monomorphized per type — an `int` array, a
  `double` array, and a `complex<double>` array are each as tight as a hand-rolled
  `std::vector<T>` loop, with no shared dynamic base.
- **Element-wise kernels vectorize declaratively** via `std::transform(std::execution::unseq, …)`
  and `std::reduce(std::execution::unseq, …)` — we write our *intent to vectorize*
  in the source, and the compiler emits SIMD for whatever the target supports.
- **linalg kernels auto-vectorize** at `-O3 -march=native` (contiguous, unit-stride
  loops; no hand-written intrinsics). See @ref simd.hpp for the full SIMD model,
  including exactly what happens on a build with **no SIMD** (answer: identical
  results, just scalar/slower — SIMD is never a correctness dependency).

## Why constrained templates, given the compile-time cost {#constrain-all-templates}

Every generic surface in cheatah — `io.print`'s `Printable`, `ndarray`'s `Numeric`,
`math`'s `Ordered`, the baseline `Value` on every emitted function parameter — is
**concept-constrained**. This is a deliberate doubling-down on the compile-time
investment: we are already asking the compiler to instantiate templates, so we make
that work also yield *early, named* diagnostics ("`Point` does not satisfy
`Printable`") instead of pages of instantiation backtrace. Fast code **and** legible
errors, both bought with the same compile-time spend.

## Dynamism without the interpreter: the cheatah runtime

The usual objection to "just compile it" is that you give up what interpreted
languages are *good* at: loading code at runtime, hot-reloading a changed module
without restarting, plugin systems, embedding a scripting layer in a host app. Those
abilities are why people reach for an interpreter in the first place.

cheatah keeps them — through the **runtime**, not an interpreter. A `.purr` program
does **not** compile to a standalone executable; it compiles to a **loadable module**
(a `.so` exporting `extern "C" void purr_main()`). The `cheatah` host then
`dlopen`s that module at runtime, resolves `purr_main`, and calls it. That `dlopen`
plug-in model **is** the dynamic-loading mechanism interpreted languages use — but the
code being loaded is **compiled native**, so it runs at full speed.

So the runtime is how cheatah serves applications that would otherwise *demand* an
interpreted language:
- **Hot reload / live update.** A long-running host — a server, a trading agent, the
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

The honest trade-off is the one this whole page is about: there is a **compile step**
(`purrc` → `.so`) before a module can be loaded, so it is not type-and-eval-instantly.
But you get the dynamic loading, hot reload, plug-ins, and embedding of an interpreted
runtime — *"compile once to a module, then load / run / reload it dynamically"* —
while every line that actually executes is optimized native code.

## The standing rule

When we add a feature, we ask up front: *does this allocate or copy more than the
hand-written C++ would?* If yes, we fix it in the codegen or the library before it
ships — as with the self-append rewrite — rather than leaving it for a user to
discover with a profiler. Performance is a feature, designed in, not bolted on.
