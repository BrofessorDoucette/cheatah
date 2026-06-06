# cheatah

> 🐆💨🔥 **Reads like a kitten, runs like hell.** cheatah is built to be as
> readable as Python *and* as fast as hand-tuned native code — so when you hit
> run, you'd better **runnn like hell** to keep up. 😼⚡

cheatah is a small, **Python-like** language (`.purr`) with a **C-style `struct`
vibe**, that **compiles to native machine code** and is hosted by the **cheatah
runtime**. It is a general-purpose language built for numerical and systems work:
data crunching, model training, numerical analysis, and scripting — at native
speed.

It is **Turing-complete**: variables, full expressions/operators, `if`/`else`,
`while`, `for`, functions, and `struct`s.

> See [`PYTHON.md`](../compiler/PYTHON.md) for the Python feature coverage and the (small) set
> of deliberate syntax deviations — most Python scripts port over with light edits.

---

## How the compiler & runtime work

cheatah is **compiled, not interpreted**. A `.purr` file becomes a native
loadable module that the runtime executable loads and runs.

```
 hello.purr
    │
    ▼  purrc  (the compiler — compiler/{lexer,parser,codegen} driven by purrc.cpp)
 ┌──────────────────────────────────────────────────────────────────┐
 │ lexer  →  tokens                                                   │
 │ parser →  AST            (recursive descent, operator precedence)  │
 │ codegen → C++ source     (exports `extern "C" purr_main()`)        │
 │ c++    →  hello.so       (a shared module; links imported stdlib)  │
 └──────────────────────────────────────────────────────────────────┘
    │
    ▼  cheatah-runtime  (the host — runtime/)
 dlopen("hello.so") → resolve `purr_main` → call it
```

- **`purrc`** ([`purrc.cpp`](../compiler/purrc.cpp)) drives lexer → parser → codegen, then
  invokes the system C++ compiler (`c++ -std=c++20 -O2 -fPIC -shared`) to build a
  loadable `.so`. The generated translation unit exports
  `extern "C" void purr_main()` — no `main()`.
- **`cheatah-runtime`** ([`../runtime/`](../runtime/)) validates the module path,
  `dlopen`s the module, resolves `purr_main`, and calls it. The module statically
  links only the stdlib it imported, so it is self-contained — the host shares no
  state with it.
- **Memory safety:** generated code uses value types, STL containers, and smart
  pointers (no raw `new`/`delete`); the AST is owned through `unique_ptr`.

```bash
purrc hello.purr -o hello.so      # compile
cheatah-runtime hello.so          # run
```

### Performance — native, zero-overhead
cheatah **transpiles to C++** and is compiled by clang at full optimization, so
there is **no interpreter, VM, or boxing** — the machine code is exactly what the
C++ compiler emits for equivalent code. Measured: a recursive `fib(35)` in
cheatah runs **at parity with hand-written C++** compiled by the same compiler.

What makes it C++-fast:
- **`-O3 -march=native -DNDEBUG -fno-semantic-interposition`** — full optimization
  and host SIMD (programs are compiled and run on the same machine).
- **User functions are `static`** (internal linkage) → the optimizer inlines them
  like local C++ functions, no exported-symbol barrier.
- **Value semantics + STL containers + monomorphized templates** — `auto` params
  become abbreviated function templates; ints are 64-bit, strings `std::string`,
  `list`/`dict` are `std::vector`/`std::unordered_map`. No indirection.
- **Static linking** of imported stdlib for production builds.

> For absolute peak, build cheatah in **release** so the (non-template) stdlib
> symbols purrc links are themselves `-O3 -march=native`. The hot, templated paths
> are already optimized into your program's `.so` regardless.

### The runtime model (fully headless)
The runtime is **fully headless** — no graphics, no GPU, no external dependencies.
It dlopens a compiled program and runs it; everything a program needs comes from
the standard library it imports. Host capabilities beyond the standard library
(for example a window or renderer) are provided by **external modules** a program
imports, not by the language core.

---

## Language

### Literals & types
`int` (`42`), `float` (`3.14`, `1e-3`), `str` (`"meow"`, escapes `\" \\ \n \t`),
`bool` (`true` / `false`). Number/strings/bools compile to C++ `long long` /
`double` / `std::string` / `bool`.

### Variables
```python
let total = 0        # new binding  -> auto total = 0;
total = total + 1    # reassignment -> total = total + 1;
```

### Operators
Arithmetic `+ - * /` (`^` maps to C++ `^`), comparison `== != < <= > >=`, logical
`and` / `or` / `not`, unary `-`. Standard precedence (`*` binds tighter than `+`;
comparisons below arithmetic; `and`/`or` lowest). Indexing `a[i]`.

### Control flow (brace blocks — the C-style vibe)
```python
if x > 0 {
    io.print("positive")
} else if x < 0 {
    io.print("negative")
} else {
    io.print("zero")
}

while n > 1 {
    n = n - 1
}

for i in range(1, 5) {     # range(stop) or range(start, stop)
    total = total + i
}
```

### Functions
```python
fn add(a, b) {             # untyped params -> C++20 abbreviated templates
    return a + b
}
fn fib(n) {                # recursion works
    if n < 2 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

### Structs (C-style)
```python
struct Point {
    label: str
    x: float
    y: float
    z: float
}

let p = Point("origin", 0.0, 0.0, 0.0)   # construction
io.print(p.z)                            # field access
```
Each `struct` compiles to a C++ `struct` (aggregate); construction uses
brace-init under the hood (`Point{...}`). Field types: `int float str bool`,
other struct names, and the containers below.

### Collections (STL-backed, memory-safe)
```python
let values = [100.0, 103.5, 99.8]        # list  -> std::vector (CTAD)
let weights = {"a": 0.6, "b": 0.4}       # dict  -> std::unordered_map
io.print(values[0], weights["a"])        # indexing
for v in values { total = total + v }    # iteration -> range-based for

struct Series {
    label: str
    points: list[float]                  # list[T]  -> std::vector<T>
    table: dict[str, float]              # dict[K,V] -> std::unordered_map<K,V>
    window: array[float, 20]             # array[T,N] -> std::array<T,N> (static)
}
```
Strings are `std::string`, so `"a" + "b"` concatenates. List/dict literals infer
their element types (so must be non-empty). All collections are RAII — no manual
memory management.

---

## Imports = include + link
`import` is the dependency declaration — a wrapper over C++ `#include` **plus
linking** the corresponding library. Nothing is available unless imported (even
`print` lives in the `io` module); a library is linked into a program **only if it
imported it** (static for production, shared for fast iteration).

```python
import io                  # include <io.hpp> + link libcheatah_io
import os.path as p        # dotted module + alias
io.print("meow")
```

## Standard library
Each module is its **own dual static + shared library** (`add_cheatah_library`
in [`../cmake/CheatahLibrary.cmake`](../cmake/CheatahLibrary.cmake)).
Templated entry points (constrained with C++20 **concepts** for clear errors) live
in headers; non-template symbols compile into the library.

- **`builtins`** *(always available, no import)* — `len`, `ord`, `chr`,
  `hex`/`oct`/`bin`, `ascii`, `hash`, `bool`/`int`/`float` conversions.
- **`io`** — `print`, `input`, `str`/`repr`/`format`, `open`/`File`.
- **`os`** — filesystem, env, process, and `os.path`.
- **`string`** — case, strip, split/join, replace, find/count, padding, classify.
- **`math`** — `abs`/`min`/`max`/`round`/`pow`, `sqrt`/trig/`log`, `gcd`/`factorial`,
  `pi`/`e`/`tau`/`inf`/`nan`.
- **`time`** — high-accuracy `<chrono>` timing: `perf_counter`/`monotonic`/`time`/
  `sleep`.
- **`ndarray`** — numpy-flavored N-d arrays with broadcasting (shared buffer +
  strides; SIMD element-wise ops).
- **`linalg`** — the SIMD linear-algebra core: `matmul`/`solve`/`inv`/`det`/`qr`/
  `svd`/`eig`/`norm`/… over `ndarray`.
- **`datetime`**, **`random`**, **`statistics`**, **`hashlib`** — dates/epochs,
  Mersenne-Twister RNG, summary statistics, and a self-contained SHA-256.

## Lexical structure
- **Comments:** `# …` or `// …` to end of line.
- **Keywords:** `and as else false fn for from if import in let not or return
  struct true while`.
- **Operators/punctuation:** `+ - * / ^ = == != < <= > >= ( ) { } [ ] , : ; .`
- **Newlines** separate statements; brace `{ }` blocks group them.

The lexer/parser never throw: errors become `Diagnostic`s (with line/column) and
parsing recovers, so one pass surfaces every error.
