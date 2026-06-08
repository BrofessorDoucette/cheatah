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
[`scripts/app_compare.py`](https://github.com/BrofessorDoucette/cheatah/blob/main/scripts/app_compare.py);
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

| op | n | cheatah | NumPy | winner |
|----|--:|--------:|------:|--------|
| `matmul` | 32 | 4.5 | 12.8 | **cheatah 2.8×** |
| `matmul` | 64 | 24 | 109 | **cheatah 4.5×** |
| `solve`  | 32 | 4.4 | 9.9  | **cheatah 2.3×** |
| `det`    | 32 | 3.4 | 9.0  | **cheatah 2.6×** |
| `inv`    | 32 | 26  | 24   | NumPy 1.1× |
| `eigvalsh` | 2 | 0.16 | 1.93 | **cheatah 11.8×** |
| `eigvalsh` | 4 | 0.93 | 2.53 | **cheatah 2.7×** |
| `eigvalsh` | 32 | 333 | 24 | NumPy 13.7× |
| `dot` | 16384 | 280 | 7.7 | **NumPy 36×** |

The pattern is consistent and unsurprising once you see it:

- **cheatah wins on small-to-medium dense factorizations** (`matmul`, `solve`, `det`,
  `inv` up to ~n=16–32). At these sizes the work is small, so NumPy's per-call Python
  dispatch *and* threaded-BLAS startup overhead dominate — cheatah's single-threaded,
  auto-vectorized C++ just does the arithmetic with no overhead.
- **`eigvalsh` is size-dependent — and small is exactly the physics case.** For a
  **few-level system** (a 2×2 spin-½ Hamiltonian, a 3- or 4-level system, a small
  effective Hamiltonian you diagonalize while sweeping a parameter), cheatah is
  *faster* than NumPy: **≈12× at n=2, ≈4× at n=3, ≈2.7× at n=4**, with the crossover
  around **n≈6–7**. When you're solving the *same small* eigenproblem millions of
  times — a parameter scan, a Floquet/Brillouin-zone sweep, a Monte-Carlo update —
  that no-dispatch speed is the number that matters. Beyond ~n=8, LAPACK's
  reduction-based solver pulls away and wins decisively (≈14× at n=32, ≈35× at n=64);
  our cyclic-Jacobi kernel does not try to compete there.
- **BLAS/LAPACK also win large `dot`** (BLAS `ddot`, ≈36× at n=16384). These are the
  routines to reach for NumPy on at scale, and where cheatah has the most headroom
  (e.g. `dot`/`solve` still copy their inputs into scratch — an avoidable cost).

So: cheatah is **not** trying to out-BLAS BLAS at scale. It wins where avoiding
interpreter and dispatch overhead matters more than a tuned kernel — which, for the
small, repeated eigenproblems a lot of physics actually runs, is squarely cheatah's
home turf — and it is honest about where the tuned kernel wins.

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
