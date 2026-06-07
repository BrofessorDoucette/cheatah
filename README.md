# cheatah 🐆

> **Programs so fast they purrrrrrrrrrrrr like a kitten.** 🐱

**cheatah** is a small, Python-like programming language that compiles to native
machine code — **Python for people who care about performance**. You write `.purr` source files,
compile them with `purrc`, and run them on the headless `cheatah` host. Because
cheatah transpiles to modern C++ and is built at `-O3 -march=native`, your
programs run at **optimized native speed** — a recursive `fib(35)` runs at parity
with hand-written C++.

> ⚠️ **Status: pre-alpha (v0.1.0-prealpha).** cheatah runs and is heavily tested
> (100+ tests; the QA gate runs them under **ASan + UBSan + Valgrind**), but the
> language and APIs may still change. Today `.purr`/`.so` are *fully trusted* — see
> [SECURITY.md](SECURITY.md) before running code you didn't write.

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

- **Python-like, with a C-style `struct` vibe.** Variables, full expressions and
  operators, `if`/`else`, `while`, `for`, functions, and `struct`s — Turing
  complete, and most Python scripts port over with light edits.
- **Compiled, not interpreted.** A `.purr` file becomes a real `.so` loadable
  module. No interpreter, no VM, no boxing — the machine code is what the C++
  compiler emits for equivalent code.
- **Native speed by construction.** `static` (internal-linkage) functions inline
  freely; ints are 64-bit, strings are `std::string`, `list`/`dict` are
  `std::vector`/`std::unordered_map`. Hot numeric loops auto-vectorize (SIMD).
- **Memory-safe codegen.** Generated code uses value types, STL containers, and
  smart pointers — no raw `new`/`delete`. (The sole exception is whatever you write
  inside a raw `cpp { … }` escape hatch — that's on you.)
- **Headless and dependency-free.** The language core and runtime have **no
  external dependencies**; the only third-party code is GoogleTest/Google
  Benchmark, used by the test suite alone.
- **Batteries included.** A standard library spanning `io`, `os`, `string`,
  `math`, `time`, `datetime`, `random`, `statistics`, `hashlib`, and a SIMD
  numeric core (`ndarray` + a full numpy-style `linalg`: `matmul`/`solve`/`inv`/
  `det`/`qr`/`svd`/`eig`/`norm`/…).

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

# Exceptions (message-based).
try {
    raise "boom"
} except e {
    io.print("caught:", e)
}

io.print(2 ** 10, math.sqrt(2.0))        # ** is power; math.* for sqrt/sin/…
io.print("hello" + " " + "world")        # strings are std::string; + concatenates
io.print("fib(10):", fib(10))
```

**Features:** `int`/`float`/`str`/`bool`, `let` variables, arithmetic + `**`
power, comparisons, `and`/`or`/`not`, `if`/`else if`/`else`, `while`, `for … in
range(…)`, `fn` functions (with recursion), `struct` records, `list`/`dict`/`array`
collections with indexing and iteration, string concatenation, `try`/`except` +
`raise`, `import` (with `as` aliases and dotted modules like `os.path`), and
always-available built-ins (`len`, `ord`, `chr`, `hex`/`oct`/`bin`, …).

### Coming from Python? A few deliberate deviations

| | cheatah | Python |
|---|---|---|
| Blocks | braces `{ }` | indentation + `:` |
| Declare / reassign | `let x = 1` / `x = 2` | `x = 1` / `x = 2` |
| Function / record | `fn f()` / `struct S` | `def f()` / `class S` |
| Print & math | `io.print`, `math.sqrt` (imported) | `print`, `math.sqrt` |
| Booleans | `true` / `false` | `True` / `False` |
| Power vs. xor | `**` is power, `^` is bitwise-xor | `**`, `^` |
| Division | `int / int` is integer division | `/` is always float |
| Types | inferred but **static** (`auto`) | dynamic |

Most Python scripts port with light edits. The full feature mapping, the remaining
deviations, and the roadmap (comprehensions, classes-with-methods, f-strings,
slicing, …) live in [compiler/PYTHON.md](compiler/PYTHON.md).

### Escape hatch: raw C++

By default cheatah looks like Python — but when you need the host language
directly, a **`cpp { … }`** block drops to raw C++ (file scope at the top level
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

> ⚠️ **Memory safety is not guaranteed inside `cpp { … }`.** Ordinary cheatah code
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

- **`purrc`** ([compiler/purrc.cpp](compiler/purrc.cpp)) drives lexer → parser
  → codegen ([compiler/](compiler/)), then invokes the system C++ compiler to
  build a loadable module. The generated translation unit exports `purr_main` — no
  `main()`.
- **`cheatah`** ([runtime/](runtime/)) is the fully headless host: it
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

- The compiler, the runtime, and the C++ it generates use **no `dynamic_cast`,
  `typeid`, or virtual dispatch**. The AST is walked with a kind-tag `enum` +
  `static_cast`, so dispatch is a branch on a tag — not a vtable lookup.
- The standard library leans on **C++20 concepts** (`requires`) and **templates**
  rather than inheritance; untyped cheatah functions lower to **abbreviated
  function templates** (`static auto f(auto a, …)`) that monomorphize and inline
  at each call site.
- **Compile-time conditionals** — concepts and preprocessor `#ifdef` — select
  implementations during compilation, so the hot path carries no dispatch cost.

The result is the same cost model as equivalent hand-written C++: value semantics,
STL containers, no boxing, no indirection.

**Near-raw C++ — the `cpp { … }` escape hatch.** Every construct already lowers
1:1 to C++ value types (`int` → `long long`, `str` → `std::string`, `list`/`dict`
→ `std::vector`/`std::unordered_map`), so cheatah reads almost like C++. When
Python-level syntax isn't enough, a **`cpp { … }`** block drops straight to raw
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
| **Lines** | 100.00% (1398/1398) |
| **Functions** | 100.00% (251/251) |
| Regions | 95.40% |
| Branches | 88.15% |
<!-- coverage:end -->

## Repository layout

| Path | What |
|------|------|
| [compiler/](compiler/) | the language toolchain (lexer/parser/codegen) and the `purrc` compiler ([purrc.cpp](compiler/purrc.cpp)) |
| [stdlib/](stdlib/) | the standard-library modules and the unit tests |
| [runtime/](runtime/) | `cheatah`, the headless host |
| [tests/](tests/) | unit, integration (purrc → runtime), and benchmark suites |
| [cmake/](cmake/) | the `add_cheatah_library` helper |
| [editors/](editors/) | editor support — a VS Code extension + TextMate grammar (highlights `.purr`, incl. embedded C++ in `cpp { … }`) |
| [scripts/](scripts/) | the QA gate and git-hook setup |

## Security

cheatah is **single-trust** today: `.purr` source and compiled `.so` modules are
fully trusted, like Python or a C++ compiler. The toolchain avoids shell/command
injection (`fork`+`execvp`, no shell), the runtime validates a module before
`dlopen`, the standard library is bounds-checked, and CI runs the whole suite under
**ASan + UBSan + Valgrind**.

The threat model, the standing review, and the plan for safely adding a Unix
interface and an MCP server (host-decided trust tiers, capability gating, and an OS
sandbox) live in [SECURITY.md](SECURITY.md). **Don't run untrusted `.purr` until the
sandboxed mode described there exists.**

## License

MIT — see [LICENSE](LICENSE).
