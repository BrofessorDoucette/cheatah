# cheatah 🐆

> **Programs so fast they purrrrrrrrrrrrr like a kitten.** 🐱

**cheatah** is a small, Python-like programming language that compiles to native
machine code — **Python for people who care about performance**. You write `.purr` source files,
compile them with `purrc`, and run them on the headless `cheatah` host. Because
cheatah transpiles to modern C++ and is built at `-O3 -march=native`, your
programs run at **optimized native speed** — a recursive `fib(35)` runs at parity
with hand-written C++.

> ⚠️ **Status: alpha (v1.11.7-alpha).** cheatah runs and is heavily tested
> (the QA gate runs the suite under **ASan + UBSan + Valgrind** at 100% coverage), but the
> language and APIs may still change. There is **no sandbox**: a `.purr`/`.so` is
> *fully trusted* by default (you can optionally sign modules and have the runtime verify
> them — see below) — read [SECURITY.md](SECURITY.md) before running code you didn't write.

```python
# hello.purr
import io

fn fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}

io.print("fib(30) =", fib(30))
```

```bash
purrc hello.purr -o hello.so      # compile to a native module
cheatah hello.so          # run it
```

---

## Why cheatah

- <b>Python-like, with a C-style `struct` vibe.</b> Variables, full expressions and
  operators, `if`/`else`, `while`, `for`, functions, and `struct`s — Turing
  complete, and most Python scripts port over with light edits.
- **Compiled, not interpreted.** A `.purr` file becomes a real `.so` loadable
  module. No interpreter, no VM, no boxing — the machine code is what the C++
  compiler emits for equivalent code.
- **Native speed by construction.** `static` (internal-linkage) functions inline
  freely; ints are 64-bit by default (opt into `i8`…`u64` for a smaller footprint),
  strings are `std::string`, `list`/`dict` are `std::vector`/`std::unordered_map`.
  Hot numeric loops auto-vectorize (SIMD).
- **Memory-safe codegen.** Generated code uses value types and STL containers —
  no raw `new`/`delete`, no manual pointer arithmetic. (The sole exception is whatever
  you write inside a raw `cpp { … }` escape hatch — that's on you.)
- **Headless and dependency-free.** The language core and runtime have **no
  external dependencies**; the only third-party code is GoogleTest/Google
  Benchmark, used by the test suite alone.
- **Batteries included.** A standard library spanning `io`, `os`, `sys`, `string`,
  `math`, `time`, `datetime`, `random`, `statistics`, `hashlib` (SHA-256/512),
  `ed25519` (from-scratch public-key signatures), `socket` (a small BSD-socket
  wrapper — enough to write an HTTP server in pure cheatah), and a SIMD numeric core
  (`ndarray` + a full numpy-style `linalg`: `matmul`/`solve`/`inv`/`det`/`qr`/`svd`/
  `eig`/`norm`/…).

## The language

cheatah reads like Python, with a C-style `{ }` structure instead of indentation.
A quick tour — this compiles and runs as-is:

```python
import io
import math

# `let` declares a variable; a bare `=` reassigns.
let total = 0
for i in range(1, 11) {          # range(stop) or range(start, stop)
    total = total + i            # 1 + 2 + … + 10  ->  55
}

# Functions (`fn`), recursion, expressions.
fn fib(n) {
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}

if total > 50 {                  # if / else if / else
    io.print("big", total)
} else {
    io.print("small", total)
}

# Collections: list -> std::vector, dict -> std::unordered_map.
let xs = [3, 1, 2]
let weights = {"a": 0.5, "b": 0.5}
for x in xs { io.print("x =", x) }       # iterate
io.print(len(xs), xs[0], weights["a"])   # len/hex/ord… are always-available built-ins

# Structs: C-style records with typed fields.
struct Point { x: float, y: float }
let p = Point(1.0, 2.0)
io.print("p:", p.x + p.y)

# Enums: scoped + type-safe (a C++ `enum class`), and they print for debugging.
enum Color { RED, GREEN, BLUE }
let c = Color.GREEN
io.print(c)                              # -> Color.GREEN

# Exceptions: errors carry a KIND, and a handler can take just the ones it knows.
try {
    raise Error("io", "disk full")
} except e of "io" {
    io.print("caught:", e, "kind:", e.kind())   # an Error prints as its message
} except e {
    io.print("something else:", e)              # last clause catches the rest
} finally {
    io.print("runs on every path")
}

io.print(2 ** 10, math.sqrt(2.0))        # ** is power; math.* for sqrt/sin/…
io.print("hello" + " " + "world")        # strings are std::string; + concatenates
io.print("fib(10):", fib(10))
```

**Features:** `int`/`float`/`str`/`bool` (plus opt-in sized integers
`i8`…`i64`/`u8`…`u64` for a smaller memory footprint — same speed, `int` stays
64-bit), `let` variables (with optional `: type`
annotations), arithmetic + `**` power, comparisons, `and`/`or`/`not`,
`if`/`elif`/`else`, `match`/`case`, `while`, `for … in range(…)`/containers with
`break`/`continue`, `fn` functions (with recursion), `struct` records, `enum`s
(scoped `enum class`, printable), `list`/`dict`/`array` collections with indexing,
**slicing** (`a[i:j]`), negative
indices, growable lists (`xs.append(v)`), and index assignment, method-call syntax
(`s.startswith("…")`), string concatenation, `try`/`except of`/`finally` + `raise`, `import`
(with `as` aliases and dotted modules like `os.path`), and always-available
built-ins (`len`, `ord`, `chr`, `hex`/`oct`/`bin`, …).

### Coming from Python? A few deliberate deviations

| | cheatah | Python |
|---|---|---|
| Blocks | braces `{ }` | indentation + `:` |
| Declare / reassign | `let x = 1` / `x = 2` | `x = 1` / `x = 2` |
| Function / record | `fn f()` / `struct S` | `def f()` / `class S` |
| Enum | `enum E { A, B }` (a C++ `enum class`) | `class E(Enum): …` |
| Print & math | `io.print`, `math.sqrt` (imported) | `print`, `math.sqrt` |
| Booleans | `true` / `false` | `True` / `False` |
| Power vs. xor | `**` is power, `^` is bitwise-xor | `**`, `^` |
| Division | `/` is always float, `//` floors | `/` is always float, `//` floors |
| Types | inferred but **static** (`auto`) | dynamic |

Most Python scripts port with light edits. The full feature mapping, the remaining
deviations, and the roadmap (comprehensions, classes-with-methods, f-strings,
slicing, …) live in [compiler/PYTHON.md](compiler/PYTHON.md).

### Escape hatch: raw C++

By default cheatah looks like Python — but when you need the host language
directly, a <b>`cpp { … }`</b> block drops to raw C++ (file scope at the top level
for `#include`s/helpers/types; inline inside a function, where it can read and
write cheatah locals):

```python
import io
cpp { static long long triple(long long n) { return n * 3; } }   # file scope
fn demo() {
    let acc = 0
    cpp { for (int i = 1; i <= 4; ++i) { acc += i; } }           # inline -> sees `acc`
    return acc + triple(2)
}
io.print(demo())                                                  # 16
```

> ⚠️ <b>Memory safety is not guaranteed inside `cpp { … }`.</b> Ordinary cheatah code
> is memory-safe (value types, STL containers, smart pointers — no raw
> `new`/`delete`), but a raw-C++ block bypasses those guarantees and is **not**
> sandboxed or checked. Raw pointers, unchecked indexing, lifetimes, and undefined
> behavior are your responsibility there — exactly as in C++.

It's optional and out of the way — see **Python for people who care about
performance** below for why this stays "all native."

## How it works

```
 hello.purr
    │
    ▼  purrc  (lexer → parser → codegen → C++ → .so)
 hello.so   (exports  extern "C" void purr_main())
    │
    ▼  cheatah
 dlopen("hello.so") → resolve purr_main → call it
```

- <b>`purrc`</b> ([compiler/purrc.cpp](compiler/purrc.cpp)) drives lexer → parser
  → codegen ([compiler/](compiler/)), then invokes the system C++ compiler to
  build a loadable module. The generated translation unit exports `purr_main` — no
  `main()`.
- <b>`cheatah`</b> ([runtime/](runtime/)) is the fully headless host: it
  validates the module path, `dlopen`s the module, resolves `purr_main`, and calls
  it. A compiled program is self-contained — it statically links the stdlib it
  imported — so the host needs no shared state with it.

See [stdlib/README.md](stdlib/README.md) for the full language reference
and [compiler/PYTHON.md](compiler/PYTHON.md) for Python coverage and the
deliberate syntax deviations.

## Python for people who care about performance — an embedded C++ DSL

cheatah is **Python for people who care about performance — without learning a
whole new language**. The aim is to land as close as possible to the *cumulative
sum* of Python and C++: Python's readability and ergonomics **plus** C++'s power
and performance, preserving as many features from *both* as it can. A Python
developer reads it on sight; a C++ developer is already at home — nobody has to
adopt an unfamiliar paradigm to get native speed.

**It's an eDSL.** An *embedded domain-specific language* lives on top of a host
language and lowers to it, reusing the host's compiler and semantics instead of
shipping its own runtime. cheatah is an eDSL over **C++**: `purrc` transpiles each
`.purr` file to ordinary C++ and hands it to a real C++ compiler
(`-O3 -march=native`). There is no interpreter or VM — the host language sits
directly underneath, and everything cheatah emits is plain, optimizable C++.
Staying an eDSL is deliberate: because it's **all native**, there is wide headroom
to keep improving performance entirely inside the compiler — better codegen,
hand-tuned SIMD/GPU kernels behind the same syntax — without changing a line of
your `.purr`.

**Compile time over runtime — no RTTI.** Because the host is C++, cheatah resolves
as much as it can at *compile time* and avoids runtime type machinery:

- The compiler, the runtime, and the C++ it generates use <b>no `dynamic_cast`,
  `typeid`, or virtual dispatch</b>. The AST is walked with a kind-tag `enum` +
  `static_cast`, so dispatch is a branch on a tag — not a vtable lookup.
- The standard library leans on **C++20 concepts** (`requires`) and **templates**
  rather than inheritance; untyped cheatah functions lower to **abbreviated
  function templates** (`static auto f(auto a, …)`) that monomorphize and inline
  at each call site.
- **Compile-time conditionals** — concepts and preprocessor `#ifdef` — select
  implementations during compilation, so the hot path carries no dispatch cost.

The result is the same cost model as equivalent hand-written C++: value semantics,
STL containers, no boxing, no indirection.

<b>Near-raw C++ — the `cpp { … }` escape hatch.</b> Every construct already lowers
1:1 to C++ value types (`int` → `long long` — or an opt-in width `i32` → `std::int32_t`,
`u8` → `std::uint8_t`, … — `str` → `std::string`, `list`/`dict`
→ `std::vector`/`std::unordered_map`), so cheatah reads almost like C++. When
Python-level syntax isn't enough, a <b>`cpp { … }`</b> block drops straight to raw
C++ — file scope at the top level (for `#include`s, helpers, types), inline inside
a function (where it can read/write cheatah locals); see **The language → Escape
hatch** above for an example. It's an *optional* escape hatch, kept off the
everyday path: the common surface stays clean and Python-simple, and you reach for
C++ only by choice. By default, cheatah looks like Python.

## Building

cheatah uses CMake (≥ 3.24) with Ninja presets. You need a C++20 compiler.

```bash
cmake --preset debug          # configure
cmake --build --preset debug  # build purrc + cheatah + stdlib
ctest --preset debug          # run the test suite
```

For optimized stdlib symbols, configure the `release` preset instead. The
`scripts/qa_gate.sh` script runs the full configure → build → test → benchmark
gate (it is also wired to the pre-push hook via `scripts/setup_hooks.sh`).

## Test coverage

The standard library is exercised by a unit test for **every function**. Coverage
is measured with clang source-based coverage (`scripts/coverage.sh`) and this table
is regenerated by the QA gate on every push:

<!-- coverage:start -->
| Metric | Standard library |
|--------|------------------|
| **Lines** | 100.00% (3891/3891) |
| **Functions** | 100.00% (1114/1114) |
| Regions | 95.3% |
| Branches | 85.6% |
<!-- coverage:end -->

## Documentation

**Read it online: [bigbrain-technology.com/docs/cheatah-docs](https://bigbrain-technology.com/docs/cheatah-docs/)** —
the full standard-library reference, every module and every function. The language's own page
is [bigbrain-technology.com/cheatah](https://bigbrain-technology.com/cheatah).

It also builds into [docs/](docs/) locally (open
[docs/html/index.html](docs/html/index.html)). Doxygen is used **only as the C++
parser** — it emits XML, and our own generator
([docs/gen-cheatah/gen.purr](docs/gen-cheatah/gen.purr), written in cheatah) renders the
site. Every stdlib function carries a Javadoc/Doxygen comment with a uniform contract:

- <b>`@param` / `@return`</b>, a one-line brief, <b>`@complexity`</b> (the Big-O runtime
  cost) and <b>`@alloc`</b> (the heap behavior — `none`, or what it allocates);
  memory behavior is a first-class part of every signature.
- an <b>`@test`</b> link to the unit test that exercises the function.

The site is a bespoke, **cheetah-themed** design: a module sidebar, per-page navigation, and
WCAG-checked contrast and reflow. It ships **zero JavaScript** — it has to work under a serving
policy of `default-src 'none'`, so the sidebar's active state is computed at generation time and
symbol search was removed rather than shipped broken. Each stdlib module also has a short
`README.md` overview. Regenerate with:

```bash
bash docs/build-docs.sh   # Doxygen XML -> generator -> docs/html/
```

## The cheatah ecosystem

cheatah's standard library is extended by separate, installable packages — each its own
public repository, built on the `purrc` toolchain and this stdlib:

- **[cheatah-space](https://github.com/BrofessorDoucette/cheatah-space)** — astronomy and
  space-physics (the `space` package).
- **[cheatah-gpu](https://github.com/BrofessorDoucette/cheatah-gpu)** — one simple GPU
  interface over Vulkan and Metal (the `gpu` package).
- **[cheatah-plot](https://github.com/BrofessorDoucette/cheatah-plot)** — dead-simple
  cross-platform plotting on the GPU (the `plot` package, built on cheatah-gpu).

Install extensions with `biome`, the package manager — see [pkg-manager/](pkg-manager/).

## Built with cheatah

cheatah is not a side project — it is the language
**[BigBrain LLC](https://bigbrain-technology.com/)** writes its own commercial software in.
A 3D [game engine and editor](https://bigbrain-technology.com/products/godspeed), a
[machine-learning training workstation](https://bigbrain-technology.com/products/ash), a
[dataset builder](https://bigbrain-technology.com/products/alice), a
[text-to-3D generator](https://bigbrain-technology.com/products/conjure) and a
[version-control module](https://bigbrain-technology.com/products/boltzmann) are all compiled
by `purrc` and run on this runtime — as does the web server delivering every one of those
pages.

See [the whole line](https://bigbrain-technology.com/products) and
[what it costs](https://bigbrain-technology.com/pricing), or start with
[what cheatah is](https://bigbrain-technology.com/cheatah).

If you are wondering whether a language this small can carry that much: the
[TLS 1.3 stack](https://bigbrain-technology.com/docs/cheatah-docs/namespacecheatah_1_1tls),
the [HTTP client](https://bigbrain-technology.com/docs/cheatah-docs/namespacecheatah_1_1requests)
and the [linear algebra](https://bigbrain-technology.com/docs/cheatah-docs/namespacecheatah_1_1linalg)
are all in this repository, written from scratch, with no OpenSSL and no BLAS anywhere in the
stack. That is the argument.

## Repository layout

| Path | What |
|------|------|
| [compiler/](compiler/) | the language toolchain (lexer/parser/codegen) and the `purrc` compiler ([purrc.cpp](compiler/purrc.cpp)) |
| [stdlib/](stdlib/) | the standard-library modules and the unit tests |
| [runtime/](runtime/) | `cheatah`, the headless host |
| [pkg-manager/](pkg-manager/) | `biome`, the package manager (cheatah logic compiled by purrc, run via a native launcher → the cheatah runtime) — scaffolds projects and pulls in optional stdlib extensions via CMake/CPM |
| [tests/](tests/) | unit, integration (purrc → runtime), and benchmark suites |
| [cmake/](cmake/) | the `add_cheatah_library` helper |
| [editors/](editors/) | editor support — a VS Code extension + TextMate grammar (highlights `.purr`, incl. embedded C++ in `cpp { … }`) |
| [scripts/](scripts/) | the QA gate and git-hook setup |
| [docs/](docs/) | the generated Doxygen API site ([Doxyfile](Doxyfile)) + the dark cheetah theme ([docs/theme/](docs/theme/)) |

## Security

cheatah is **single-trust by default** (no sandbox): a `.purr`/`.so` runs with your
full privileges, like a Python script or a C++ program you compile. The toolchain
avoids shell/command injection (`fork`+`execvp`, no shell), the runtime validates a
module before `dlopen`, the standard library is bounds-checked, and the QA gate runs the
whole suite under **ASan + UBSan + Valgrind**. Modules can also be **signed** (`purrc
--sign`, from-scratch Ed25519) and the runtime can **verify** them before loading
(`CHEATAH_VERIFY=strict`) — authenticity and integrity, not a sandbox.

The threat model, the standing review, and the plan for safely adding a Unix
interface and an MCP server (host-decided trust tiers, capability gating, and an OS
sandbox) live in [SECURITY.md](SECURITY.md). <b>Don't run untrusted `.purr` until the
sandboxed mode described there exists.</b>

## License

MIT — © 2026 BigBrain LLC. Lead engineer and producer: Joshua Doucette, on behalf of
BigBrain LLC. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Acknowledgments

cheatah is original, dependency-free work, but it stands on the shoulders of the
open-source community — Python, the C++ standard, NumPy/SciPy/Matplotlib,
BLAS/LAPACK, Eigen, GLM, and more. Credit where it's due:
[ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md).
