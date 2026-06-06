# cheatah 🐆

> **Programs so fast they purrrrrrrrrrrrr like a kitten.**

**cheatah** is a small, Python-like programming language that compiles to native
machine code — **Python for people who care about performance**. You write `.purr` source files,
compile them with `purrc`, and run them on the headless `cheatah` host. Because
cheatah transpiles to modern C++ and is built at `-O3 -march=native`, your
programs run at **optimized native speed** — a recursive `fib(35)` runs at parity
with hand-written C++.

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
  smart pointers — no raw `new`/`delete`.
- **Headless and dependency-free.** The language core and runtime have **no
  external dependencies**; the only third-party code is GoogleTest/Google
  Benchmark, used by the test suite alone.
- **Batteries included.** A standard library spanning `io`, `os`, `string`,
  `math`, `time`, `datetime`, `random`, `statistics`, `hashlib`, and a SIMD
  numeric core (`ndarray` + a full numpy-style `linalg`: `matmul`/`solve`/`inv`/
  `det`/`qr`/`svd`/`eig`/`norm`/…).

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
Python-level syntax isn't enough, a **`cpp { … }`** block drops you straight to
raw C++ — at the **top level** it lands at *file scope* (for `#include`s, helper
functions, and types); **inside a function** it's emitted *inline*:

```python
import io

cpp {                                  # file scope — includes, helpers, types
    static long long triple(long long n) { return n * 3; }
}

fn demo() {
    let acc = 0
    cpp { for (int i = 1; i <= 4; ++i) { acc += i; } }   # inline — sees `acc`
    return acc + triple(2)             # call C++ from cheatah → 10 + 6
}

io.print(demo())                       # 16
```

It's an *optional* escape hatch, kept off the everyday path: the common surface
stays clean and Python-simple, and you reach for C++ only by choice. By default,
cheatah looks like Python.

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

## Repository layout

| Path | What |
|------|------|
| [compiler/](compiler/) | the language toolchain (lexer/parser/codegen) and the `purrc` compiler ([purrc.cpp](compiler/purrc.cpp)) |
| [stdlib/](stdlib/) | the standard-library modules and the unit tests |
| [runtime/](runtime/) | `cheatah`, the headless host |
| [tests/](tests/) | unit, integration (purrc → runtime), and benchmark suites |
| [cmake/](cmake/) | the `add_cheatah_library` helper |
| [scripts/](scripts/) | the QA gate and git-hook setup |

## License

MIT — see [LICENSE](LICENSE).
