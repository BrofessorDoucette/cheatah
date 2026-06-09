# Changelog

All notable changes to cheatah. This project is **pre-alpha** — expect breaking
changes between releases.

## v0.8.0-alpha — Python-3 division + the whole library, documented

The `/` operator is now **true division** (always a float, even `int / int`), matching
Python 3, and integer/floor division moves to an opt-in `//` operator. Alongside it, the
docs site grew to cover the **entire** standard library: every module's README now ships
on its page, and the linalg-vs-NumPy numbers live beside the functions they measure.

### Language — division (**breaking**)
- **`/` is true division.** `6 / 4 == 1.5`, and even an exact `6 / 2` is a `double`. The
  operator lowers to `cheatah::builtins::truediv`.
- **`//` is opt-in floor division**, flooring toward −∞ like Python (`-7 // 2 == -4`,
  `7.0 // 2.0 == 3.0`) — `cheatah::builtins::floordiv`. As a result `//` is **no longer a
  comment**; comments are `#` only.
- *Porting:* a `/` that you relied on for integer division becomes `//`.

### Docs — the whole library on the site
- **Per-module READMEs now render on each module's page.** The examples and prose from
  `stdlib/<mod>/README.md` are merged in as the overview above that module's reference, so
  the worked examples and explanations that previously lived only in the repo are now
  served. (`compiler/PYTHON.md` and the changelog are guide pages too.)
- **The linalg-vs-NumPy comparison moved to the linalg page**, beside the functions it
  measures (element-wise array math to the ndarray page). Each numeric function's
  **Performance** row now shows its own measured vs-NumPy number (µs/op + operand size +
  faster/slower), instead of a generic pointer.
- **Tighter prose.** A pass over the guides trimmed wordiness without dropping facts,
  examples, or the single-threaded-by-design framing.

## v0.7.0-alpha — cross-platform: Linux, macOS, and Windows

cheatah now builds and runs on **Linux, macOS, and Windows**. The language, the `purrc`
interface, and every standard-library API are **unchanged** — this release is purely
structural: the compile → link → load pipeline became platform-aware, so a `.purr`
program compiles to the host's native loadable module and the runtime loads it, on each
OS. (Linux is verified end-to-end; macOS and Windows use standard platform APIs behind
detection and want on-device confirmation.)

### Portability
- **One place for the differences** — new `cmake/Portability.cmake` detects, per
  compiler/OS/arch: the native-arch flag (`-march=native`, falling back to `-mcpu=native`
  on Apple Silicon), the loadable-module extension (`.so` / `.dylib` / `.dll`), the
  vector-math library, and the flag/link lists `purrc` passes the C++ backend. The rest
  of the build (and `purrc`) just consumes them, so no `#ifdef` sprawl.
- **`purrc`** consumes the baked flags; spawns the compiler via `fork`+`execvp` (POSIX)
  or `_spawnvp` (Windows); emits the platform module extension.
- **Runtime** validates the host's binary format — **ELF** (Linux), **Mach-O** incl.
  fat/universal (macOS), **PE** (Windows) — and loads via `dlopen` (POSIX) or
  `LoadLibrary` (Windows). *(Fixes "refusing to load … not an ELF shared object" on
  macOS, where a `.so` is really a Mach-O dylib.)*
- **Codegen** exports `purr_main` through a portable macro (`extern "C"`, plus
  `__declspec(dllexport)` so a Windows DLL exposes the entry point).
- **stdlib** — `socket` gains `SO_NOSIGPIPE` (macOS) and a Winsock backend (Windows);
  `os` gains the Windows `getpid`/`setenv` shims.

### SIMD acceleration, per platform
- **Auto-vectorization on every platform** via the detected native-arch flag (the bulk:
  products, factorizations, `sqrt`).
- **Vector transcendentals** through the platform's vector libm where one ships:
  **libmvec** (Linux), **Accelerate** (macOS), opt-in **SVML** (Windows,
  `-DCHEATAH_WIN_SVML=ON`); scalar-but-correct fallback otherwise.

## v0.6.0-alpha — winning the numerics: world-class linear algebra + SIMD ufuncs

This release is a ground-up performance pass on the numeric core. We benchmarked every
`linalg` and `ndarray` routine honestly against NumPy/LAPACK, hunted down *why* we lost
where we lost, and fixed the causes. The result: cheatah now **matches or beats** NumPy
on every dense linear-algebra routine measured at the small-to-moderate sizes most
scientific code runs at — products, the LU family, the SVD (and its `pinv`/`cond`/
`matrix_rank` derivatives), and the symmetric eigensolver — and the element-wise math
ufuncs now beat NumPy's too. The recurring villains were two: heap allocations hiding in
element access, and inner loops that couldn't vectorize.

### Linear algebra — algorithms and kernels
- **Killed the per-element heap allocation in matrix/vector extraction.** The extractors
  read elements through `a.at({i, j})`, and the `{i, j}` braced index **heap-allocated a
  `std::vector` per element** — pulling out an n×n matrix did n² allocations before any
  math. Replaced with direct contiguous reads (`memcpy` fast path; strided walk for
  views). This alone is a large speedup across *every* routine.
- **Zero-copy reads for the read-only routines.** `dot`/`matmul`/`outer`/`trace`/`norm`/
  `cholesky`/`kron`/`conj_transpose` (real **and** complex) now operate straight on the
  array's own buffer when it's contiguous — they allocate only their result.
- **`dot` beats BLAS `ddot`.** The reduction was a serial floating-point dependency chain
  that can't vectorize without `-ffast-math`; rewritten with independent accumulators so
  `-O3 -march=native` issues SIMD+FMA. 16384-element `dot`: **280µs → 3.7µs**, from 36×
  *slower* than NumPy to **2.1× faster**.
- **`inv` wins.** It was `n` serial-reduction back-substitutions (un-vectorizable);
  rewritten as a whole-identity block solve whose inner loops are vectorizable SAXPYs.
  32×32: **26µs → 5.7µs**, from NumPy-1.1× to **cheatah 4.2×**. (`det` already won — same
  LU, but its SAXPY update vectorizes; that contrast was the tell.)
- **Symmetric eigensolver: cyclic Jacobi → Householder tridiagonalization + implicit-shift
  QL** (the method LAPACK uses). `eigvalsh` went from losing **10–35×** to **tying
  LAPACK** from 16×16 up, winning decisively below; it also skips the eigenvector
  accumulation it used to compute and throw away. The complex-Hermitian path rides the
  same solver via the 2n real embedding.
- **SVD: one-sided Jacobi → Golub–Reinsch** (Householder bidiagonalization + implicit-shift
  QR), reimplemented entirely **column-major** so the bidiagonalization reflectors and the
  QR's whole-column U/V rotations vectorize. The full decomposition now **beats NumPy
  1.7–1.8×** (so `pinv` wins 1.3–1.4×); a new values-only path **ties LAPACK**, and so do
  `cond`/`matrix_rank`. Replacing the correctly-rounded `std::hypot` in the O(n²) Givens
  rotations with the faster EISPACK `pythag` was the final unlock. 64×64 `svd` went from
  **3505µs (NumPy 18.8×) to a 1.8× win**.
- **New `linalg.svdvals(a)`** — singular values only (≈ `numpy.linalg.svd(compute_uv=False)`):
  skips U/V accumulation and the dominant U/V rotations.
- **Fewer allocations everywhere:** hoisted per-iteration working buffers out of loops
  (`inv`, `qr`, the general eigensolver), factor-the-complex-LU-once in inverse iteration,
  single-copy hand-off into the eigen solvers.

### ndarray — element-wise math beats NumPy
- **SIMD transcendentals via libmvec.** `ndarray.exp`/`sin`/`cos`/`log`/`tan`/`sqrt`/`cbrt`
  for contiguous `double` arrays route through an isolated kernel TU (`ufunc_simd.cpp`)
  compiled `-fveclib=libmvec -fno-math-errno`, so they vectorize through glibc's vector
  math — **without** `-ffast-math`, so results stay strictly IEEE and the rest of cheatah's
  arithmetic is untouched. `exp` now wins ≈3×, `sin` ≈5× at 16384 elements.
- **`array ⊕ scalar` broadcasting fast path.** It was doing a bounds-checked `at()` per
  element (no SIMD); a contiguous fast path took `ndarray.add` from **≈20× slower than
  NumPy to ~even**, speeding up every scalar-broadcast op.
- **`purrc` now passes `-fno-math-errno`** (lets `sqrt`/algebraic math vectorize; strictly
  IEEE, unlike `-ffast-math`) and links `-lm` for the libmvec symbols.

### Docs, benchmarks & tooling
- **Honest, comprehensive vs-NumPy comparison** on the performance page: ~25 routines
  across dimensions, full operand shapes stated (a 2×2 matrix, a 16384-element vector,
  n×n ⊗ n×n), green/red speedup styling, and the NumPy version compared against.
- `scripts/numpy_compare.py` expanded to the whole library and made **fair** (full-SVD vs
  full-SVD, values-vs-values); new `scripts/linalg_sweep.py` finds per-function crossovers.
- Doc accuracy pass: `@alloc` rows now match the code (zero-copy where it is), algorithm
  names updated (Golub–Reinsch, tridiagonal QL), test names highlighted in the rendered
  tables.

### Fixes
- Corrected stale `@alloc`/behavior annotations found in an audit (e.g. `dot`'s "2-D
  matmul" comment — it rejects non-vector 2-D input; complex `dot`/`vdot` scratch claims).

## v0.5.0-alpha — performance, honestly: @perf everywhere, vs-CPython & vs-NumPy, and a smarter editor

This release is about **measuring** cheatah honestly and surfacing those numbers
where you work. Every standard-library function now carries a measured **Performance**
row; the benchmarks compare against both interpreted CPython and NumPy/LAPACK (and
report where cheatah *loses*, not just where it wins); the numeric core gains
element-wise math; and the VS Code extension becomes a real reference tool.

### Performance & benchmarks
- **`@perf` on every function.** The reference docs and editor hover now show a measured
  *Performance* row per function — cheatah ns/call vs the honest baseline (CPython, or
  **NumPy** for the numeric modules) with the version it was measured against. Numbers
  live in one provenance-tagged `docs/perf_data.json`, regenerated periodically by
  `scripts/perf_suite.py` (NOT in the QA gate — benchmarks are noisy/machine-specific).
- **Honest benchmark methodology.** Benchmarks are **elision-proof** (vary input +
  accumulate + print, so the optimizer can't delete the work — a naive loop measured a
  bogus "125000×"). cheatah is ~**20–35×** faster than CPython on real loops, ~**1×**
  where the work is already native (`hashlib`), and the whole-program suite (Mandelbrot,
  N-body, RK4, integral) runs **14–97×** faster.
- **cheatah vs NumPy, by dimension.** cheatah *wins* small/medium dense `matmul`/`solve`/
  `det`/`inv` and small `eigvalsh` (the few-level-Hamiltonian physics case) by avoiding
  Python/dispatch overhead; NumPy's BLAS/LAPACK win at scale and on `dot`/large `eigvalsh`.
  Reported both ways. (Large-dimension speedups are the future **cheatah-gpu** story.)
- **No garbage collector** — memory safety is RAII scopes + `shared_ptr` refcounting, so
  there are no GC pauses; documented on the performance page.
- The benchmark harnesses (`app_compare`, `perf_compare`, `numpy_compare`) are themselves
  **rewritten in pure cheatah** (dogfooding); the QA gate stays in trusted tooling.

### Numeric core (`ndarray`)
- **Element-wise math ufuncs** — `sqrt`/`cbrt`/`exp`/`log`/`sin`/`cos`/`tan`/`abs` over a
  whole array (the array forms of the scalar `math` module; ≈ NumPy ufuncs),
  SIMD-vectorized on the contiguous fast path.

### VS Code extension
- **Richer hover** — each function shows a divided facts block: **Performance** (@perf),
  **Complexity**, **Allocation**, and the **tests** that cover it, with icons.
- **Go to Definition (Ctrl-click)** on a stdlib call or an imported module opens its C++
  header — resolved against the cheatah **runtime you pick** (`cheatah.root` setting),
  the workspace, or headers **bundled with the extension** (kept in sync with the built
  runtime).
- **User structs & interfaces** — hover a type for its definition (fields/methods/
  interfaces), a method/field for its doc; Ctrl-click jumps to the declaration.
- **Module names are colored**, and the QA gate now **auto-reinstalls the extension**
  from the freshly-built runtime, so the editor never drifts (it had been stuck on
  v0.2.0). C++ IntelliSense for the benchmark sources fixed.

### Docs
- The performance page leads with the three benchmark comparisons grouped together.
- Audited every doc for accuracy and fixed broken/again-runnable examples across the
  guides and module READMEs.

## v0.4.0-alpha — complex linear algebra

cheatah becomes a tool for **complex** linear algebra — the kind physics (quantum,
plasma) and signal processing actually need. The numeric core can now store and
operate on complex numbers, the `linalg` module gains complex inner-product spaces
and Hermitian eigensolvers, and a real matrix finally yields the **complex
eigenvalues it mathematically has** instead of throwing. The docs site grows three
new guides (Getting Started, Coming from Python, Security).

### Numeric core (`ndarray`)
- **Complex element type.** The array element constraint widened from `Numeric` to
  **`Field`** — a real arithmetic type *or* a `std::complex` of a floating type — so
  complex matrices and vectors are first-class. Complex arrays print Python-style
  (`a+bj` / `a-bj`).
- **Construct & inspect complex arrays:** `complex(re, im)`, `real(a)`, `imag(a)`,
  `conj(a)`.
- `io.print` renders complex scalars Python-style too (`8+3j`, not `(8,3)`).

### Linear algebra (`linalg`)
- **Complex spectra.** `eig` / `eigvals` on a general real matrix now return the
  **complex** eigenvalues (a rotation gives ±i) *and* complex eigenvectors (inverse
  iteration) — they no longer throw on a complex conjugate pair. `eigh` / `eigvalsh`
  return the guaranteed-real spectrum (the numpy split).
- **Complex Hermitian eigensolver.** `eigh` / `eigvalsh` accept a complex Hermitian
  matrix → real eigenvalues, complex eigenvectors (the quantum-mechanics workhorse),
  via a real symmetric 2n embedding.
- **Complex inner-product spaces:** complex `dot` (bilinear) and `vdot`
  (conjugate-linear Hermitian inner product), complex `matmul`, and `conj_transpose`
  (the Hermitian adjoint Aᴴ).

### Docs
- New site guides: **Getting Started** (with `.purr` → `.so` compile + static/dynamic
  link diagrams), **Coming from Python** (porting guide), and **Security** (built-in
  protections vs. what you still own).
- **Syntax highlighting** in the generated site's code blocks — a lightweight,
  theme-matched highlighter that reuses the compiler's own keyword set.
- Fixed code-block rendering in the generated site (spaces inside `<pre>` were being
  collapsed). `compiler/PYTHON.md` now documents struct **methods** and
  **interfaces**, not just data classes.

### Not yet
- Templating the real decomposition routines over a generic `FloatingPoint` element
  type (float32) is deferred to a follow-up — float arrays aren't constructible from
  cheatah source yet.

## v0.3.0-alpha — a real language: methods, interfaces, generic numerics, and IntelliSense

Third pre-alpha, and the largest yet. This release turns cheatah from a small
scripting core into a **statically-typed, concept-driven language**: structs gain
methods and interfaces (lowered to C++ concepts), the numeric core becomes
**generic over its element type**, and the editor gets full **IntelliSense**. A
standing rule lands too — *every* template is concept-constrained, so misuse yields
a named error, never template spam.

### Language
- **Control flow:** `break`, `continue`, `elif`, and `match`/`case`.
- **Collections:** growable lists (`xs.append(v)` / `append(xs, v)`), index
  assignment (`d[k] = v`, `xs[i] = v`), and empty typed declarations
  (`let xs: list[int] = []`).
- **Slicing & indexing:** `a[i:j]` (and `a[i:]`, `a[:j]`), **negative indices**, and
  Python-style string indexing (`s[i]` is a length-1 string, so `s[i] == "<"`).
- **Method-call syntax:** `obj.method(...)` (UFCS) — including string predicates
  `s.startswith/endswith/contains`.
- **Struct methods:** declare `fn method(self, …)` in a struct body → a real C++
  member function (`self` is implicit; non-mutating methods are emitted `const`).
- **Interfaces:** `interface Shape { fn area(self) … }` lowers to a **C++20 concept**;
  `struct Circle : Shape { … }` adds a compile-time `static_assert` (a struct that
  doesn't fulfill it fails with *"Circle must fulfill Shape"*, not a template dump);
  and `fn describe(s: Shape)` becomes a concept-constrained `auto` — static
  polymorphism, no inheritance, no vtables.

### Numerics — generic over the element type
- **`ndarray` is now `basic_ndarray<T>`** over any `Numeric` element type, **deduced
  from the literals** (`array([1,2,3])` is integer, `array([1.0,…])` is double).
  `NDArray` remains the default `basic_ndarray<double>`, so existing code is unchanged.
- **Declarative SIMD:** element-wise ops use `std::transform(std::execution::unseq, …)`
  and `sum` uses `std::reduce(unseq)` — vectorized for any `T` — with a correct
  C-order fallback for broadcast/strided views. The linalg SIMD model (pure
  auto-vectorization, and the no-SIMD behavior) is now documented in `simd.hpp`.
- A `Numeric` / `FloatingPoint` concept split is in place for the linalg
  generalization to come.

### io
- **`Printable` protocol:** `io.print` / `io.str` now render **lists, dicts,
  structs, and ndarrays** — a value is printable if it streams, exposes a `str()`
  method, or is a container of printables (recursive). `NDArray` gained a `str()`.

### Performance
- **Automatic string-concatenation optimization:** the compiler rewrites
  `x = x + a + b` into in-place appends (`x += a; x += b`), turning O(n²)
  string-building into O(n) with no full-length temporaries — an ease-of-development
  guarantee, no manual `+=`/builder needed.
- A new **[Performance](docs/performance.md)** docs page documents the
  compile-time-for-run-time bargain.

### Compiler & policy
- **Constrain-all-templates:** every emitted function/method parameter is a
  concept-constrained `auto` (the baseline `Value`), and every library template
  carries a concept (`Numeric`, `Ordered`, `Printable`, `Sized`, …). No
  unconstrained templates anywhere — comprehensible compile errors by construction.

### Tooling
- **VS Code extension → IntelliSense.** Hover any stdlib/builtin function for its
  signature, params, and docs; type `module.` for autocomplete. Backed by a
  generated `functions.json` (built from the Doxygen XML), plus a "Get Started"
  walkthrough. The extension is now versioned with the language.
- **QA gate enforces the extension stays in sync** — a new hard-gate stage
  regenerates the extension hover DB from the stdlib API and fails the push if it
  drifted, so the editor never ships stale relative to the library.
- The docs generator now renders hand-written **guide pages** (e.g. Performance).

### Quality
- **100% unit-test line + function coverage** and **100% Javadoc** maintained across
  all the new code, under the existing ASan + UBSan + Valgrind QA gate.

## v0.2.0-alpha — networking, a bespoke docs site, and a three-tier test system

Second pre-alpha. The language core is unchanged; this release adds **networking**
to the standard library, replaces the documentation pipeline with our **own
site generator**, and builds out a **three-tier, per-function test system** behind
a stricter QA gate.

### Standard library
- **New `socket` module** — a thin, memory-safe BSD-socket wrapper
  (`tcp_listen`/`tcp_connect`/`accept`/`send`/`sendall`/`recv`/`bind`/`listen`/
  `connect`/`close`/`local_port`/`last_error`, …). `import socket`.
- A **pure-cheatah docs server** (`scripts/serve-docs.purr`) written entirely in
  `.purr` on top of `socket` — no `cpp { }`, no raw pointers — that serves the
  generated site over HTTP. Proof that real programs can be written in cheatah.

### Documentation
- **Bespoke documentation site.** Doxygen is now used *only* as the C++ parser
  (it emits XML); our own generator (`docs/gen/generate.py`) renders a modern
  static site — left module sidebar, client-side symbol search, a source browser,
  a light/dark toggle, cache-busted assets, and accessible (WCAG 2.1 AA) contrast.
  Replaces doxygen-awesome entirely.
- **Structured doc tags.** The old combined `@note` is split into `@complexity`
  (Big-O) and `@alloc` (heap behavior); every function also links **three test
  kinds** — `@test` (unit), `@crtest` (compile-run), `@systest` (system) — straight
  to their source. **100% Javadoc coverage** of the public stdlib, plus a
  behavioral description for ~190 functions.

### Testing
- **Three-tier, per-function tests:** a C++ **unit** test and a **compile-run**
  test (compile a `.purr` calling the function, run it on the runtime, assert exact
  stdout) for every function; a **comprehensive per-module system** test that
  exercises *every* function of its module; and six **cross-module system apps**
  (GradeReport, LinearSolve, EventLog, Integrity, MonteCarlo, NetworkRoundtrip)
  that only pass if many modules cooperate.

### Quality & security
- The QA gate now **hard-fails** below **100% unit-test line+function coverage** and
  below **100% Javadoc coverage**. ASan/UBSan + Valgrind run across all test tiers.
- The ~200-test per-function compile-run battery is **opt-in** (`QA_GATE_FULL_CR=1`)
  so the default gate stays fast.
- On a passing push to `main`, the docs site is **auto-regenerated**.

## v0.1.0-prealpha — first pre-alpha

The first tagged pre-alpha of the cheatah language: it compiles and runs, with a
standard library, an editor extension, and CI that runs every test under two memory
checkers.

### Language
- Python-like surface, C-style `{ }` blocks, compiles to native code via `purrc`
  (lexer → parser → codegen → C++), run by the headless `cheatah` runtime.
- `let` variables; `int`/`float`/`str`/`bool`; full operators incl. `**` (power);
  `if`/`else if`/`else`, `while`, `for … in range(…)`; `fn` functions (recursion);
  `struct` records; `list`/`dict`/`array` collections; `try`/`except` + `raise`;
  `import` with `as` aliases and dotted modules.
- **`cpp { … }` raw-C++ escape hatch** — file scope at the top level, inline inside
  a function (memory safety is the author's responsibility there).
- **`;`** is an optional statement separator/terminator (and struct-field separator).

### Standard library
`builtins`, `io`, `os`, `string`, `math`, `time`, `datetime`, `random`,
`statistics`, `hashlib`, and a SIMD numeric core (`ndarray` + numpy-style `linalg`).
Each module builds as both a static and shared library.

### Tooling
- VS Code extension ([editors/vscode/](editors/vscode/)): syntax highlighting
  (with embedded C++ in `cpp { … }`) and a "Seti + cheetah" file icon theme.
- `purrc --version` / `cheatah --version`.

### Quality & security
- 100+ tests (unit + purrc→runtime end-to-end). QA gate (pre-push hook) runs them
  under **ASan + UBSan** and **Valgrind**, plus release benchmarks.
- Security review + hardening (ndarray overflow/OOB guards); threat model and the
  Unix-interface/MCP safe-design plan in [SECURITY.md](SECURITY.md).

### Known limitations
- Single-trust model — do **not** run untrusted `.purr` yet (no sandbox).
- No Unix system-call interface or MCP server yet (designed, not built).
- Native GitHub `.purr` highlighting pending a github-linguist submission.
