# cheatah 🐆

> **Reads like a kitten, runs like hell.**

**cheatah** is a small, Python-like programming language that compiles to native
machine code. You write `.purr` source files, compile them with `purrc`, and run
them on the headless `cheatah-runtime` host. Because cheatah transpiles to modern
C++ and is built at `-O3 -march=native`, your programs run at **optimized native
speed** — a recursive `fib(35)` runs at parity with hand-written C++.

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
cheatah-runtime hello.so          # run it
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
 hello.so   (exports  extern "C" void purr_main(cheatah::runtime::Runtime&))
    │
    ▼  cheatah-runtime
 dlopen("hello.so") → resolve purr_main → call it with a live Runtime
```

- **`purrc`** ([purrc/](purrc/)) drives lexer → parser → codegen
  ([purrscript/](purrscript/)), then invokes the system C++ compiler to build a
  loadable module. The generated translation unit exports `purr_main` — no
  `main()`.
- **`cheatah-runtime`** ([runtime/](runtime/)) is the fully headless host: it
  `dlopen`s the module, resolves `purr_main`, and calls it. Capabilities beyond
  the standard library are provided by external modules a program imports, not by
  the language core.

See [purrscript/README.md](purrscript/README.md) for the full language reference
and [purrscript/PYTHON.md](purrscript/PYTHON.md) for Python coverage and the
deliberate syntax deviations.

## Building

cheatah uses CMake (≥ 3.24) with Ninja presets. You need a C++20 compiler.

```bash
cmake --preset debug          # configure
cmake --build --preset debug  # build purrc + cheatah-runtime + stdlib
ctest --preset debug          # run the test suite
```

For optimized stdlib symbols, configure the `release` preset instead. The
`scripts/qa_gate.sh` script runs the full configure → build → test → benchmark
gate (it is also wired to the pre-push hook via `scripts/setup_hooks.sh`).

## Repository layout

| Path | What |
|------|------|
| [purrscript/](purrscript/) | the language toolchain (lexer/parser/codegen) and the standard-library modules |
| [purrc/](purrc/) | `purrc`, the compiler |
| [runtime/](runtime/) | `cheatah-runtime`, the headless host |
| [tests/](tests/) | unit, integration (purrc → runtime), and benchmark suites |
| [cmake/](cmake/) | the `add_purrscript_library` helper |
| [scripts/](scripts/) | the QA gate and git-hook setup |

## License

MIT — see [LICENSE](LICENSE).
