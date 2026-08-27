# cheatah

> 🐆💨🔥 **Programs so fast they purrrrrrrrrrrrr like a kitten.** 🐱 cheatah is built
> to be as readable as Python *and* as fast as hand-tuned native code — so when you
> hit run, your programs **purrrrrrrrrrrrr**. 😼⚡

cheatah is a small, **Python-like** language (`.purr`) with a <b>C-style `struct`
vibe</b>, that **compiles to native machine code** and is hosted by the **cheatah
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
    ▼  cheatah  (the host — runtime/)
 dlopen("hello.so") → resolve `purr_main` → call it
```

- <b>`purrc`</b> ([`purrc.cpp`](../compiler/purrc.cpp)) drives lexer → parser → codegen, then
  invokes the C++ compiler cheatah was built with (`-std=c++20 -O3 -march=native -fPIC -shared`,
  the full list in [`cmake/Portability.cmake`](../cmake/Portability.cmake)) to build a loadable `.so`. The generated translation unit exports
  `extern "C" void purr_main()` — no `main()`.
- <b>`cheatah`</b> ([`../runtime/`](../runtime/)) validates the module path,
  `dlopen`s the module, resolves `purr_main`, and calls it. The module statically
  links only the stdlib it imported, so it is self-contained — the host shares no
  state with it.
- **Memory safety:** generated code uses value types, STL containers, and smart
  pointers (no raw `new`/`delete`); the AST is owned through `unique_ptr`.

```bash
purrc hello.purr -o hello.so      # compile
cheatah hello.so          # run
```

### Performance — native, zero-overhead
cheatah **transpiles to C++** and is compiled by the host C++ compiler at full optimization, so
there is **no interpreter, VM, or boxing** — the machine code is exactly what the
C++ compiler emits for equivalent code. Measured: a recursive `fib(35)` in
cheatah runs **at parity with hand-written C++** compiled by the same compiler.

What makes it C++-fast:
- <b>`-O3 -march=native -DNDEBUG -fno-semantic-interposition`</b> — full optimization
  and host SIMD (programs are compiled and run on the same machine).
- <b>User functions are `static`</b> (internal linkage) → the optimizer inlines them
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
`int` (`42`), `float` (`3.14`, `1e-3`), `str` (`"meow"`, escapes `\n \t \r \0 \" \\`),
`bool` (`true` / `false`). Number/strings/bools compile to C++ `long long` /
`double` / `std::string` / `bool`.

### Variables
```python
let total = 0        # new binding  -> auto total = 0LL;
total = total + 1    # reassignment -> total += 1LL;
```

### Operators
Arithmetic `+ - * / % // **` (`//` floors, `**` lowers to `std::pow`, `^` is C++ `^`), augmented
assignment `+= -= *= /=`, comparison `== != < <= > >=`, logical `and` / `or` / `not`, unary `-`.
Standard precedence (`*` binds tighter than `+`; comparisons below arithmetic; `and`/`or` lowest). Indexing `a[i]`.

### Control flow (brace blocks — the C-style vibe)
<!-- purr: fragment -->
```purr
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
brace-init under the hood (`Point{...}`). Field types: `int float str bool`, sized
numbers (`i8`…`u64`, `f32`), other struct and enum names, `ndarray<T>`, and the containers below.

### Collections (STL-backed, memory-safe)
```python
let values = [100.0, 103.5, 99.8]        # list  -> std::vector (CTAD)
let weights = {"a": 0.6, "b": 0.4}       # dict  -> std::unordered_map
io.print(values[0], weights["a"])        # indexing
for v in values { total = total + v }    # iteration -> range-based for

struct Series {
    label: str
    points: list<float>                  # list<T>  -> std::vector<T>
    table: dict<str, float>              # dict<K,V> -> std::unordered_map<K,V>
    window: array<float, 20>             # array<T,N> -> std::array<T,N> (static)
}
```
Strings are `std::string`, so `"a" + "b"` concatenates. List/dict literals infer
their element types; an empty literal needs an annotation (`let xs: list<int> = []`).
All collections are RAII — no manual memory management.

---

## Imports = include + link
`import` is the dependency declaration — a wrapper over C++ `#include` **plus
linking** the corresponding library. Nothing is available unless imported (even
`print` lives in the `io` module); a library is linked into a program **only if it
imported it**, as its static archive `libcheatah_<m>.a`.

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

- <b>`builtins`</b> *(always available, no import)* — `len`, `ord`, `chr`,
  `hex`/`oct`/`bin`, `ascii`, `hash`, `bool`/`int`/`float` conversions.
- <b>`io`</b> — `print`, `input`, `str`/`repr`/`format`, `open`/`File`.
- <b>`os`</b> — filesystem, env, process, and `os.path`.
- <b>`string`</b> — case, strip, split/join, replace, find/count, padding, classify.
- <b>`math`</b> — `abs`/`min`/`max`/`round`/`pow`, `sqrt`/trig/`log`, `gcd`/`factorial`,
  `pi`/`e`/`tau`/`inf`/`nan`.
- <b>`time`</b> — high-accuracy `<chrono>` timing: `perf_counter`/`monotonic`/`time`/
  `sleep`.
- <b>`ndarray`</b> — numpy-flavored N-d arrays with broadcasting (shared buffer +
  strides; SIMD element-wise ops).
- <b>`linalg`</b> — the SIMD linear-algebra core: `matmul`/`solve`/`inv`/`det`/`qr`/
  `svd`/`eig`/`norm`/… over `ndarray`.
- <b>`datetime`</b>, <b>`random`</b>, <b>`statistics`</b>, <b>`hashlib`</b>, <b>`ed25519`</b> —
- <b>`aead`</b>, <b>`x25519`</b>, <b>`p256`</b>/<b>`p384`</b>, <b>`tls`</b>, <b>`socket`</b>, <b>`websocket`</b>, <b>`requests`</b>,
  <b>`parsers`</b>, <b>`regex`</b>, <b>`memory`</b>, <b>`thread`</b>, <b>`sys`</b>, <b>`fixarray`</b> — the from-scratch crypto and
  networking stack, input parsers, the lazy-DFA regex engine, ownership and threads, `sys.argv`, and
  fixed-extent arrays; one line on each in [`docs/mainpage.md`](../docs/mainpage.md).

## Escape hatch: raw C++ (`cpp { … }`)
By default cheatah looks like Python. When you need the full power of the host
language, a <b>`cpp { … }`</b> block drops to **raw C++**, emitted verbatim:

```python
import io

cpp {                                  // TOP LEVEL → file scope
    #include <numeric>                 // add headers, helper functions, types
    static long long sum_to(long long n) {
        long long s = 0;
        for (long long i = 1; i <= n; ++i) s += i;
        return s;
    }
}

fn run() {
    let total = 0
    cpp {                              // INSIDE a function → emitted inline
        total = sum_to(100);           // raw C++ can read/write cheatah locals
    }
    return total
}

io.print(run())                        # 5050
```

- **Placement is the rule:** a `cpp` block at the **program top level** is emitted
  at **file scope** (so it can hold `#include`s, free functions, and types the rest
  of the program calls); a `cpp` block **inside a function or block** is emitted
  **inline** at that point and can read/write the surrounding cheatah variables.
- The block body is captured **verbatim** — the brace matcher understands C++
  string/char literals and `//` / `/* */` comments, so braces inside them don't
  confuse it. (Exotic cases — digit separators like `1'000`, or raw string literals
  `R"(...)"` containing unbalanced braces — can; wrap or avoid those.)
- `cpp` is only special immediately before `{` on the same line; elsewhere it's an
  ordinary identifier.
- ⚠️ <b>Memory safety is not guaranteed inside `cpp { … }`.</b> Ordinary cheatah code
  is memory-safe (value types, STL containers, smart pointers — no raw
  `new`/`delete`), but a raw-C++ block is emitted verbatim and is **not** sandboxed
  or checked: raw pointers, unchecked indexing, lifetimes, and undefined behavior
  are your responsibility, exactly as in C++.

The standard library already includes `<array> <cmath> <memory> <stdexcept>
<string> <unordered_map> <utility> <vector>` plus `builtins.hpp` and every module
you `import`, so a lot of raw C++ needs no extra `#include` at all.

## Lexical structure
- **Comments:** `# …` to end of line (`//` is floor division, not a comment).
- **Keywords:** `and as break case continue elif else enum except false fn for from
  if import in interface let match not or raise return struct true try while with`.
  `of` and `finally` are **contextual** — recognised after `except` / a try block, but
  still usable as ordinary names anywhere else.
- **Operators/punctuation:** `+ - * / % // ** ^ & = += -= *= /= == != < <= > >= ( ) { } [ ] , : ; .`
- **Newlines** separate statements; brace `{ }` blocks group them. A <b>`;`</b> is an
  *optional* statement separator/terminator — `let a = 1; let b = 2` or a trailing
  `x = x + 1;` both work (and `;` may separate `struct` fields). It's never required.

The lexer/parser never throw: errors become `Diagnostic`s (with line/column) and
parsing recovers, so one pass surfaces every error.
