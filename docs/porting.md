# Coming from Python {#porting}

<div class="cheetah-slogan">🐱 <em>Bring your Python; leave the interpreter behind.</em> 🐆</div>

cheatah is **Python-shaped**: most scripts port with light, mechanical edits. This
is the practical checklist for moving a `.py` file to a `.purr` — what maps
one-to-one, the deliberate syntax deviations, and the gotchas worth knowing. (For
*why* the compiled result is fast, see @ref performance.)

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
| Indexing & slicing | `s[0]`, `s[1:3]`, `xs[i] = v` | same (no step/`a[1:3]=…` yet) |
| Strings | `"a" + "b"`, `s.startswith(p)`, `s.contains(x)` | `+`, `.startswith`, `in` |
| Loops | `for x in xs { … }`, `while c { … }` | same logic |
| Control flow | `if/elif/else`, `break`, `continue`, `match` | same |
| Built-ins | `len(x)`, `hex(n)`, `ord(c)` (no import) | `len`, `hex`, `ord` |
| Errors | `try { … } except e { … }`, `raise "msg"` | message-based, catch-all |

Standard-library modules mirror Python names: `import math` (`math.sqrt`),
`import string`, `import random`, `import statistics`, `import hashlib`,
`import os` / `os.path`, `import time`, `import datetime`. Numeric work uses
`import ndarray` (numpy-flavored arrays) and `import linalg` (numpy.linalg-style
routines).

## Structs: more than a dataclass

A cheatah `struct` started as a `@dataclass`, but now carries **typed fields**,
**methods**, and **interfaces** — so most small Python classes port directly.

cheatah:

```
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

the same in Python:

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
  inherits** — it stays a bag of fields + methods that *implements* an interface. For
  a hierarchy, you **refine interfaces**: one interface builds on another, and (like
  C++ concept subsumption — *if predicate A holds then B holds*) anything satisfying
  the refined interface also satisfies the ones it refines. So a struct implements a
  single interface and inherits nothing; the interface graph carries all the "is-a"
  structure.
- **Construction is positional** over the fields in declaration order
  (`Circle(2.0)`); there is **no custom constructor / `__init__`** yet. Field access
  is `c.r`.

## Deliberate deviations (and why)

1. **Braces, not indentation.** Keeps the lexer simple (no INDENT/DEDENT) and the
   structure explicit. *Porting:* replace `:`+indent with `{ … }`.
2. **`let` declares, `=` reassigns.** A clear declaration point maps to C++ `auto`.
   *Porting:* add `let` on a name's first assignment.
3. **Everything is imported — including `print`.** `print` is `io.print`; math
   helpers (`abs`/`min`/`max`/`pow`/`round`) live in `math`. *Why:* the compiler
   links exactly what you use. Global built-ins (`len`/`hex`/`ord`) need no import.
4. **Static types under the hood.** `let`/params use C++ `auto`, so a name's type is
   fixed by its initializer — no dynamic re-typing (`x = 1; x = "s"` won't compile).
5. **Numeric operators.** `**` is power (`std::pow`); `^` is bitwise-xor. Division
   matches Python 3: `/` is **true division** (always a float, even `int / int`),
   and `//` is **floor division** (floors toward −∞).
6. **Containers are STL types.** `list[T]` → `std::vector<T>`, `dict[K,V]` →
   `std::unordered_map<K,V>`. Literals use type inference, so an **empty** `[]`/`{}`
   needs a type annotation. Iterating a `dict` yields **key/value pairs**.
7. **Exceptions are message-based & catch-all.** `except e` binds `e` to the message
   string; `raise "msg"` throws. No typed `except`, `as`, or `finally` yet.

## A worked port

Python:

```python
import statistics

def zscores(xs):
    m = statistics.mean(xs)
    sd = statistics.pstdev(xs)
    return [(x - m) / sd for x in xs]

print(zscores([1, 2, 3, 4]))
```

cheatah:

```
import io
import statistics

fn zscores(xs) {
    let m = statistics.mean(xs)
    let sd = statistics.pstdev(xs)
    let out: list[float] = []
    for x in xs {
        out.append((x - m) / sd)
    }
    return out
}

io.print(zscores([1.0, 2.0, 3.0, 4.0]))
```

Two edits beyond syntax: the **comprehension** becomes an explicit loop (not yet
supported), and an **empty list literal needs a type annotation** (`let out:
list[float] = []`) — cheatah infers a list's element type from its literal elements,
so an empty `[]` has nothing to infer from.

## Not yet supported (roadmap)

Comprehensions; typed exceptions & `finally`; **interface refinement** (one
interface inheriting another) and **custom constructors**; f-strings (use
`io.format`); slice **assignment** and **step** slices; tuples/unpacking; `with`;
generators/`yield`; keyword/default arguments; `lambda`. All tracked toward
frictionless Python → cheatah porting.

**Struct inheritance is a non-goal**, by design — structs stay simple and only
*implement* interfaces; any "is-a" hierarchy lives in the interface graph (see
above).

---

Once it compiles, it runs as optimized native code — see @ref performance for the
compile-time-for-run-time bargain and how the `@perf` numbers are measured.
