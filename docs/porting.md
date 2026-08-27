# Coming from Python {#porting}

<div class="cheetah-slogan">🐱 <em>Bring your Python; leave the interpreter behind.</em> 🐆</div>

cheatah is **Python-shaped**: most scripts port with light, mechanical edits. This is the
practical checklist for moving a `.py` file to a `.purr` — what maps one-to-one, the
deliberate syntax deviations, and the gotchas worth knowing.

The canonical, always-current feature matrix lives in
[`compiler/PYTHON.md`](https://github.com/BrofessorDoucette/cheatah/blob/main/compiler/PYTHON.md);
this page is the narrative version.

## The 60-second diff

| Python | cheatah | Note |
|--------|---------|------|
| `x = 1` (first time) | `let x = 1` | `let` *declares*; bare `=` reassigns |
| `def f(a, b):` | `fn f(a, b) { … }` | `fn`, braces instead of `:`+indent |
| `if c:` / `for x in xs:` | `if c { … }` / `for x in xs { … }` | braces, no colon |
| `class Bar:` | `struct Bar { … }` | typed fields, methods, interfaces (below) |
| `print(x)` | `import io` → `io.print(x)` | even `print` is imported |
| `2 ** 10` / `a ^ b` | `2 ** 10` / `a ^ b` | `**` is power; `^` is **bitwise-xor**, not power |
| `7 / 2` / `7 // 2` | `7 / 2` / `7 // 2` | identical to Python 3: `/` always floats, `//` floor-divides |
| `True` / `False` | `true` / `false` | lowercase |

Indentation is purely cosmetic in cheatah — newlines separate statements and `{ }`
groups them. A `;` is an *optional* separator, never required.

## What maps one-to-one

No rethinking needed — just the syntax shell above:

| Concept | cheatah | Python |
|---------|---------|--------|
| Lists & growth | `let xs = [1, 2, 3]`, `xs.append(4)` | `xs.append(4)` |
| Dicts | `{"k": 1}`, `d[k] = v` | same |
| Indexing & slicing | `s[0]`, `s[1:3]`, `xs[i] = v`, `xs[1:3] = ys` | same (no step slices) |
| Strings | `"a" + "b"`, `s.startswith(p)`, `s.contains(x)` | `+`, `.startswith`, `in` |
| Loops | `for x in xs { … }`, `while c { … }` | same logic |
| Control flow | `if/elif/else`, `break`, `continue`, `match` | same |
| Resources | `with io.open(p) as f { … }` | `with open(p) as f:` |
| Built-ins | `len(x)`, `hex(n)`, `ord(c)` (no import) | `len`, `hex`, `ord` |
| Errors | `try { … } except e of "kind" { … } finally { … }`, `raise "msg"` | kind-based, not typed |

Standard-library modules mirror Python names: `import math` (`math.sqrt`),
`import string`, `import random`, `import statistics`, `import hashlib`,
`import os` / `os.path`, `import time`, `import datetime`. Numeric work uses
`import ndarray` (numpy-flavored arrays) and `import linalg` (numpy.linalg-style
routines).

## Structs: more than a dataclass

A cheatah `struct` carries **typed fields**, **methods**, and **interfaces**, so most
small Python classes port directly.

SIDEBYSIDE: cheatah | Python

```purr
import io

interface Shape {
    fn area(self)
}

struct Circle : Shape {
    r: float
    fn area(self) {
        return 3.14159 * self.r * self.r
    }
}

fn describe(s: Shape) {
    return s.area()
}

let c = Circle(2.0)
io.print(c.area())
```

```python
from typing import Protocol

class Shape(Protocol):
    def area(self) -> float: ...

@dataclass
class Circle:
    r: float
    def area(self):
        return 3.14159 * self.r ** 2

def describe(s: Shape):
    return s.area()

c = Circle(2.0)
print(c.area())
```

- **Methods** take `self` first (`fn area(self) { … }`) and are called with method
  syntax (`c.area()`) — they compile to real C++ member functions.
- **Interfaces** are C++20 *concepts*: `interface Shape { fn area(self) }` lists the
  required methods, and `struct Circle : Shape { … }` makes the compiler **verify at
  compile time** that `Circle` fulfills `Shape` (static, not duck-typed). Typing a
  parameter by an interface (`fn describe(s: Shape)`) constrains what may be passed —
  fast, no virtual dispatch, and enables patterns like strategy.
- **Inheritance lives in the interfaces, not the structs.** A struct **never
  inherits** — it stays a bag of fields + methods that *implements* one or more
  interfaces. Interfaces today are **flat** (refining one interface from another is on
  the roadmap, not in yet); struct inheritance is a deliberate non-goal, so the
  interface graph is where any "is-a" structure will live.
- **Construction is positional** over the fields in declaration order
  (`Circle(2.0)`); there is <b>no custom constructor / `__init__`</b> yet. Field access
  is `c.r`.

## Deliberate deviations (and why)

1. **Braces, not indentation.** Keeps the lexer simple (no INDENT/DEDENT) and the
   structure explicit. *Porting:* replace `:`+indent with `{ … }`.
2. <b>`let` declares, `=` reassigns.</b> A clear declaration point maps to C++ `auto`.
   *Porting:* add `let` on a name's first assignment.
3. <b>Everything is imported — including `print`.</b> `print` is `io.print`; math
   helpers (`abs`/`min`/`max`/`pow`/`round`) live in `math`. *Why:* the compiler
   links exactly what you use. Global built-ins (`len`/`hex`/`ord`) need no import.
4. **Static types under the hood.** `let`/params use C++ `auto`, so a name's type is
   fixed by its initializer — no dynamic re-typing (`x = 1; x = "s"` won't compile).
5. **Numeric operators.** `**` is power (`std::pow`); `^` is bitwise-xor. Division
   matches Python 3: `/` is **true division** (always a float, even `int / int`),
   and `//` is **floor division** (floors toward −∞).
6. **Containers are STL types.** `list<T>` → `std::vector<T>`, `dict<K,V>` →
   `std::unordered_map<K,V>`. Literals use type inference, so an **empty** `[]`/`{}`
   needs a type annotation. Iterating a `dict` yields **key/value pairs**.
7. **Exceptions select on a KIND, not a type.** `except e of "index" { … }` matches an
   error's kind string; a bare `except e { … }` catches the rest and belongs last.
   `raise "msg"` raises kind `"error"`, `raise Error("kind", "msg")` names one, and a
   bare `raise` inside a handler re-raises. `finally { … }` runs on every exit path.
   There is no exception *hierarchy* — cheatah has no inheritance, so kinds are open
   strings rather than classes. An error no handler claims keeps travelling.
8. <b>`with` is RAII, not a context-manager protocol.</b> `with expr [as name] { … }`
   binds a resource for the block and its destructor releases it on **every** exit path
   (`return`/`break`/exception) — the direct analog of Python's `with open(…) as f:`.
   There is <b>no `__enter__`/`__exit__`</b>: any value with a destructor works (`io.open`,
   `socket.open`/`serve`, `tls.open`, `websocket.open`/`open_url` all return owning
   guards). No `with a, b:` multi-context form yet — nest blocks.

## Worked ports

A few complete `.py` → `.purr` ports — cheatah on the left, the original Python on the right.

**Fibonacci + a running sum** — the syntax shell and nothing else; the logic is identical:

SIDEBYSIDE: cheatah | Python

```purr
import io

fn fib(n) {
    if n < 2 { return n }
    return fib(n-1) + fib(n-2)
}

let total = 0
for i in range(1, 11) {
    total = total + i
}
io.print("sum:", total)
io.print("fib(10):", fib(10))
```

```python
def fib(n):
    if n < 2:
        return n
    return fib(n-1) + fib(n-2)

total = 0
for i in range(1, 11):
    total = total + i
print("sum:", total)
print("fib(10):", fib(10))
```

**Z-scores** — two edits beyond syntax: the comprehension becomes an explicit loop (not
supported yet), and the empty list literal needs a type annotation, since an empty `[]`
gives the compiler nothing to infer an element type from:

SIDEBYSIDE: cheatah | Python

```purr
import io
import statistics

fn zscores(xs) {
    let m = statistics.mean(xs)
    let sd = statistics.pstdev(xs)
    let out: list<float> = []
    for x in xs {
        out.append((x - m) / sd)
    }
    return out
}

io.print(zscores([1.0, 2.0, 3.0, 4.0]))
```

```python
import statistics

def zscores(xs):
    m = statistics.mean(xs)
    sd = statistics.pstdev(xs)
    return [(x - m) / sd for x in xs]

print(zscores([1, 2, 3, 4]))
```

**Error handling** — `try` / `except` work as you'd expect; the name binds the error,
which prints and compares as its message and also carries `.kind()`. Add `of "kind"` to
handle one class of failure and let the rest travel:

SIDEBYSIDE: cheatah | Python

```purr
import io

fn parse(s) {
    try {
        return int(s)
    } except e {
        return -1
    }
}

io.print(parse("42"), parse("oops"))
```

```python
def parse(s):
    try:
        return int(s)
    except ValueError:
        return -1

print(parse("42"), parse("oops"))
```

**Numerics** — NumPy's array ops map onto the `ndarray` + `linalg` core (literals carry
their element type, so write `2.0`, not `2`):

SIDEBYSIDE: cheatah | Python (NumPy)

```purr
import io
import ndarray
import linalg

let A = ndarray.array([[2.0, 1.0], [1.0, 3.0]])
let b = ndarray.array([1.0, 2.0])
io.print(ndarray.to_string(linalg.solve(A, b)))
```

```python
import numpy as np

A = np.array([[2.0, 1.0], [1.0, 3.0]])
b = np.array([1.0, 2.0])
print(np.linalg.solve(A, b))
```

**Dicts** — `{k: v}` literals and `d[k]` indexing are the same; the key/value types are
inferred from the literal:

SIDEBYSIDE: cheatah | Python

```purr
import io

let ages = {"ada": 36, "linus": 54}
io.print(ages["ada"])
```

```python
ages = {"ada": 36, "linus": 54}
print(ages["ada"])
```

## Not yet supported (roadmap)

Comprehensions; **interface refinement** (one interface inheriting another) and **custom
constructors**; f-strings (use `io.format`); **step** slices;
tuples/unpacking; generators/`yield`; `lambda`. All tracked toward frictionless Python →
cheatah porting.

**Struct inheritance is a non-goal**, by design — structs stay simple and only
*implement* interfaces; any "is-a" hierarchy lives in the interface graph (see
above).

## A new habit worth picking up: move work to compile time

The deepest difference isn't syntax — it's *when* work happens. Python is an interpreter:
essentially everything, including deciding which branch of an `if` to take, happens **at
run time**, every time the line is reached. The whole philosophy of C++ (and so of cheatah,
which compiles to it) is the opposite: **do as much as possible once, at compile time**, so
the running program only does the work that genuinely depends on runtime data.

cheatah hands you a tool Python simply doesn't have: a <b>compile-time `if`</b>. Bind a value
with `constexpr let` and an `if` over it is resolved *while compiling* — the losing branch
isn't just skipped at runtime, it is **never compiled into the program at all**:

```purr
import io

fn log(msg) {
    constexpr let DEBUG = false      # a compile-time constant
    if DEBUG {                       # decided at COMPILE time, not run time
        io.print("[debug]", msg)
    } else {
        io.print(msg)
    }
}
```

With `DEBUG = false`, the `[debug]` branch is gone from the binary entirely — there is no
check, no dead code, only the `else`. Flip it to `true` and the other branch vanishes instead.
Python cannot do this: `if DEBUG:` there is a runtime test on every single call, forever.

**The mindset shift for Python migrants:** ask *"is this known when I compile, or only when I
run?"* Configuration flags, table sizes, feature toggles, unit conversions, algorithm choices
fixed at build time — make them `constexpr` and let the compiler bake the decision in. It is
the same instinct as choosing the right data structure, one level earlier: you are choosing
*when* the work happens. purrc then picks the cheapest legal lowering automatically
(`if constexpr` → `switch` → an `if`/`else` chain) — see
[Optimizations → Branch selection](optimizations.html#compile-time).

---

Once it compiles, it runs as optimized native code — see @ref performance for the
compile-time-for-run-time bargain and how the Performance numbers are measured.
