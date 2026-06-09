# cheatah ↔ Python

A living document of **what Python features cheatah supports** and the **small
set of deliberate deviations** from Python syntax (chosen for simplicity and a
clean, fast compile to C++). Goal: **most Python scripts port to cheatah with
light edits.** Update this as the language grows.

> TL;DR of the deviations: **blocks use `{ }` not indentation**; **`let`** declares
> a variable; **`fn`** not `def`; **`struct`** not `class`; **everything is
> imported** (even `print`, from `io`); types are **inferred and static** under the
> hood.

---

## ✅ Supported (and how it maps to Python)

| Feature | cheatah | Python |
|---|---|---|
| Comments | `# …` | `# …` |
| Int / float / str / bool | `42`, `3.14`, `"hi"`, `true`/`false` | same, but `True`/`False` |
| Variable declaration | `let x = 1` | `x = 1` |
| Reassignment | `x = x + 1` | `x = x + 1` |
| Arithmetic | `+ - * /` | same |
| Power | `2 ** 10` (→ `std::pow`) | `2 ** 10` |
| Comparison | `== != < <= > >=` | same |
| Logical | `and` `or` `not` | same |
| `if` / `elif` / `else` | `if c { … } elif c { … } else { … }` | `if/elif/else:` |
| `match` / `case` | `match x { case 1 { … } case _ { … } }` | `match/case` |
| `while` | `while c { … }` | `while c:` |
| `break` / `continue` | `break`, `continue` | same |
| `for` over a range | `for i in range(a, b) { … }` | `for i in range(a, b):` |
| Function def | `fn f(a, b) { return a + b }` | `def f(a, b):` |
| Exceptions | `try { … } except e { … }`, `raise "msg"` | `try/except`, `raise` |
| Recursion | works | works |
| Function call | `f(1, 2)` | same |
| Indexing | `a[i]` (negative OK; `s[i]` → 1-char str) | same |
| Slicing | `a[i:j]`, `a[i:]`, `a[:j]`, `a[-3:]` | same |
| List literal | `[1, 2, 3]` → `std::vector` | `[1, 2, 3]` |
| Empty typed list/dict | `let xs: list[int] = []`, `let d: dict[str,int] = {}` | `xs = []`, `d = {}` |
| Growable list | `xs.append(v)` / `append(xs, v)` | `xs.append(v)` |
| Dict literal | `{"k": 1}` → `std::unordered_map` | `{"k": 1}` |
| Index assignment | `xs[i] = v`, `d[k] = v` | same |
| Iterate a container | `for x in xs { … }` | `for x in xs:` |
| String concatenation | `"a" + "b"` (strings are `std::string`) | same |
| String predicates | `s.startswith(p)`, `s.endswith(s)`, `s.contains(x)` | `s.startswith`, `in` |
| Method-call syntax | `xs.append(v)` (UFCS → `cheatah::builtins`) | `obj.method(...)` |
| Module import | `import io`, `import os.path as p` | same |
| Member access | `b.close`, `os.path.join(...)` | same |
| Records | `struct Bar { close: float }` | `@dataclass class Bar:` |
| Methods | `fn pct(self) { … }` inside the struct, called `b.pct()` | `def pct(self):` |
| Interfaces | `interface Shape { fn area(self) }` + `struct Circle : Shape { … }` | `Protocol` / ABC |
| Enums | `enum Color { RED, GREEN, BLUE }`, used as `Color.RED` | `class Color(Enum): …` |
| Construction / fields | `Bar(...)`, `b.close` | `Bar(...)`, `b.close` |
| `print` | `io.print(x)` | `print(x)` |
| Built-ins | `len(x)`, `hex(n)`, `ord(c)` (no import) | `len`, `hex`, `ord` |

True / False are written **lowercase** (`true` / `false`).

### Enums

`enum` declares a **scoped, type-safe** enumeration — it lowers to a C++ `enum
class`, not a plain C `enum`, so members never implicitly convert to integers and
are always reached through the enum's name:

```python
# cheatah                                  # Python
enum Color {                                  from enum import Enum
    RED                                       class Color(Enum):
    GREEN                                          RED = 1
    BLUE                                           GREEN = 2
}                                                  BLUE = 3

enum Status { OK = 0, WARN = 1, FAIL = 2 }    # members reached as Color.RED
```

- Members are separated by newlines, commas, or semicolons.
- A member may carry an explicit value (`OK = 0`); without one it follows C++
  rules (the previous value plus one, starting at 0).
- Access members through the enum name — `Color.RED` (→ `Color::RED`). Compare with
  `==`/`!=`, `match` on them, store them in `struct` fields (`state: Color`), and
  pass them to functions.
- They **print** for debugging: `io.print(Color.RED)` shows `Color.RED` (and they
  work in `io.format` and inside printed lists/dicts too), just like Python. An
  out-of-range value (e.g. from a `cpp { … }` cast) shows `Color(<n>)`.

### Command-line programs

`import sys` exposes `sys.argv` (a `list[str]` — `sys.argv[0]` is the program
name, `sys.argv[1:]` the arguments), just like Python. `purrc` always compiles a
program to a loadable **module** (`.so`/`.dylib`/`.dll`); the `cheatah` runtime
runs it and forwards the command-line arguments into `sys.argv`:

```sh
purrc app.purr -o app.so      # a module (purrc never emits a standalone binary)
cheatah app.so one two        # sys.argv == ["app.so", "one", "two"]
```

To ship a program as its own command (so users type `app …`, not `cheatah app.so
…`), build a tiny native **launcher** that invokes the runtime on the module —
that is what `cheatah_add_program()` and the `biome` package manager do. Compiled
cheatah code therefore always runs under the runtime, never standalone.

---

## 🔀 Deliberate deviations from Python (and why)

1. **Blocks use braces `{ }`, not indentation + colons.**
   ```python
   # cheatah                # Python
   if x > 0 {                  if x > 0:
       f()                         f()
   }
   ```
   *Why:* the lexer stays simple (no INDENT/DEDENT), and braces give C-style
   structure. **Porting:** replace `:` + indentation with `{ … }`.

2. **`let` declares a new variable; bare `=` reassigns.**
   `let x = 1` then `x = 2`. *Why:* a clear declaration point maps to C++
   `auto x = …;`. **Porting:** add `let` on first assignment of a name.

3. **`fn` instead of `def`.** `fn add(a, b) { return a + b }`.

4. **`struct` instead of `class` — with methods and interfaces.**
   A `struct` is more than a data class: it carries **typed fields**, **methods**,
   and can declare the **interfaces** it fulfills.
   ```python
   # cheatah                              # Python
   interface Shape {                         from typing import Protocol
       fn area(self)                         class Shape(Protocol):
   }                                             def area(self) -> float: ...

   struct Circle : Shape {                   @dataclass
       r: float                              class Circle:
       fn area(self) {                           r: float
           return 3.14159 * self.r * self.r      def area(self):
       }                                             return 3.14159 * self.r ** 2
   }

   let c = Circle(2.0)                       c = Circle(2.0)
   io.print(c.area())                        print(c.area())
   ```
   - **Fields** are typed (`int float str bool`, a container, or another struct).
   - **Methods** take `self` first (`fn area(self) { … }`), are called with method
     syntax (`c.area()`), and compile to real member functions.
   - **Interfaces** are C++20 *concepts*: `interface Shape { fn area(self) }` lists
     required methods, and `struct Circle : Shape { … }` makes the compiler **verify
     statically** that `Circle` fulfills `Shape` (a `static_assert` at compile time —
     not duck-typed at runtime). A parameter typed by an interface
     (`fn describe(s: Shape) { … }`) constrains what may be passed — fast (no virtual
     dispatch) and enough for patterns like the strategy pattern.
   - **Inheritance lives in interfaces, not structs.** A struct **never inherits** —
     it stays a simple bag of fields + methods that *implements* an interface.
     Hierarchies come from **refining interfaces** (one building on another),
     mirroring C++ concept subsumption (*if predicate A holds then B holds*):
     satisfying a refined interface implies satisfying the ones it refines. So the
     interface graph carries all the "is-a" structure and structs stay simple.
   - **No custom constructor / `__init__`** yet — construction is **positional** over
     the fields in declaration order (`Circle(2.0)`, `Bar("d", 1.0)`).

5. **Everything is imported — including `print`.** `print` lives in `io` (`import io`
   then `io.print(...)`); the math functions `abs`/`min`/`max`/`round`/`pow` live in
   `math` (`math.abs(...)`), not as globals. *Why:* explicit dependencies = the
   compiler links exactly what you use. **Porting:** add the relevant `import` and
   qualify (`print` → `io.print`, `sqrt` → `math.sqrt`).
   *(Truly global built-ins like `len`/`hex`/`ord` need no import.)*

6. **`^` is bitwise-xor (C++), not power.** Use `**` for exponentiation (it maps
   to `std::pow`), e.g. `2 ** 10`.

7. **`/` is true division; `//` floors.** Like Python 3, `/` always yields a float
   (even `int / int`), and `//` floor-divides toward −∞.

8. **Containers map to STL types.** `list[T]` → `std::vector<T>`, `dict[K,V]` →
   `std::unordered_map<K,V>`, `array[T,N]` → `std::array<T,N>` (static). List/dict
   **literals** infer element types via C++ CTAD, so they must be **non-empty**
   (an empty `[]`/`{}` needs a type annotation). Iterating a `dict` yields
   key/value **pairs** (not keys like Python).

9. **Statically typed under the hood (type inference).** `let`/params use C++
   `auto`, so a variable's type is fixed by its initializer. No dynamic re-typing
   (`x = 1; x = "s"` won't work).

10. **No indentation significance.** Newlines separate statements; `{ }` groups
    them; indentation is purely cosmetic. A **`;`** is an *optional* statement
    separator/terminator (`let a = 1; let b = 2`, a trailing `x = x + 1;`, or to
    separate `struct` fields) — handy if you're used to Python's `;` or C++'s, but
    never required.

11. **Exceptions are message-based & catch-all.** `try { … } except e { … }`
    catches any error and binds `e` to the **message string** (not an exception
    object); `raise "msg"` throws a generic error. No typed `except ValueError`,
    `as`, or `finally` yet.

---

## 🚧 Not yet supported (roadmap)

Comprehensions; typed exceptions & `finally`; **interface refinement** (one
interface inheriting another) and **custom constructors / `__init__`** (structs
already have methods + interfaces — see #4); f-strings & rich string formatting (use
`io.format`); slice **assignment** (`a[1:3] = …`) and step slices (`a[::2]`);
tuples/unpacking; `with` statements; generators/`yield`; keyword/default arguments;
`lambda`.

All tracked toward frictionless Python → cheatah porting.
**Struct inheritance is a non-goal** by design — structs stay simple and only
*implement* interfaces; all "is-a" structure lives in the interface graph (#4).

---

## A ported example

```python
# Python                                # cheatah
def fib(n):                             fn fib(n) {
    if n < 2:                               if n < 2 { return n }
        return n                            return fib(n-1) + fib(n-2)
    return fib(n-1) + fib(n-2)          }

total = 0                               let total = 0
for i in range(1, 11):                  for i in range(1, 11) {
    total = total + i                       total = total + i
print("sum:", total)                    }
print("fib(10):", fib(10))              io.print("sum:", total)
                                        io.print("fib(10):", fib(10))
                                        # ...with `import io` at the top
```
