# cheatah ↔ Python

A living document of **what Python features cheatah supports** and the **small
set of deliberate deviations** from Python syntax (chosen for simplicity and for a
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
| Comments | `# …` or `// …` | `# …` |
| Int / float / str / bool | `42`, `3.14`, `"hi"`, `true`/`false` | same, but `True`/`False` |
| Variable declaration | `let x = 1` | `x = 1` |
| Reassignment | `x = x + 1` | `x = x + 1` |
| Arithmetic | `+ - * /` | same |
| Power | `2 ** 10` (→ `std::pow`) | `2 ** 10` |
| Comparison | `== != < <= > >=` | same |
| Logical | `and` `or` `not` | same |
| `if` / `else if` / `else` | `if c { … } else if c { … } else { … }` | `if/elif/else:` |
| `while` | `while c { … }` | `while c:` |
| `for` over a range | `for i in range(a, b) { … }` | `for i in range(a, b):` |
| Function def | `fn f(a, b) { return a + b }` | `def f(a, b):` |
| Exceptions | `try { … } except e { … }`, `raise "msg"` | `try/except`, `raise` |
| Recursion | works | works |
| Function call | `f(1, 2)` | same |
| Indexing | `a[i]` | same |
| List literal | `[1, 2, 3]` → `std::vector` | `[1, 2, 3]` |
| Dict literal | `{"k": 1}` → `std::unordered_map` | `{"k": 1}` |
| Iterate a container | `for x in xs { … }` | `for x in xs:` |
| String concatenation | `"a" + "b"` (strings are `std::string`) | same |
| Module import | `import io`, `import os.path as p` | same |
| Member access | `b.close`, `os.path.join(...)` | same |
| Records | `struct Bar { close: float }` | `@dataclass class Bar:` |
| Construction / fields | `Bar(...)`, `b.close` | `Bar(...)`, `b.close` |
| `print` | `io.print(x)` | `print(x)` |
| Built-ins | `len(x)`, `hex(n)`, `ord(c)` (no import) | `len`, `hex`, `ord` |

True / False are written **lowercase** (`true` / `false`).

---

## 🔀 Deliberate deviations from Python (and why)

1. **Blocks use braces `{ }`, not indentation + colons.**
   ```python
   # cheatah                # Python
   if x > 0 {                  if x > 0:
       f()                         f()
   }
   ```
   *Why:* the lexer stays simple (no INDENT/DEDENT), and braces give the intended
   C-style structure. **Porting:** replace `:` + indentation with `{ … }`.

2. **`let` declares a new variable; bare `=` reassigns.**
   `let x = 1` then `x = 2`. *Why:* a clear declaration point maps to C++
   `auto x = …;`. **Porting:** add `let` on first assignment of a name.

3. **`fn` instead of `def`.** `fn add(a, b) { return a + b }`.

4. **`struct` instead of `class` (data classes only, for now).**
   ```python
   # cheatah                       # Python
   struct Bar {                       @dataclass
       date: str                      class Bar:
       close: float                       date: str
   }                                      close: float
   ```
   Fields are **typed** (`int float str bool` or another struct). No methods,
   inheritance, or `__init__` yet — construction is positional (`Bar("d", 1.0)`).

5. **Everything is imported — including `print`.** `print` lives in `io`, so
   `import io` then `io.print(...)`. The math functions `abs`/`min`/`max`/`round`/
   `pow` live in `math` (`math.abs(...)`), not as globals. *Why:* explicit
   dependencies = the compiler links exactly what you use. **Porting:** add the
   relevant `import` and qualify (`print` → `io.print`, `sqrt` → `math.sqrt`).
   *(Truly global built-ins like `len`/`hex`/`ord` need no import.)*

6. **`^` is bitwise-xor (C++), not power.** Use `**` for exponentiation (it maps
   to `std::pow`), e.g. `2 ** 10`.

7. **`/` uses C++ numeric semantics.** `int / int` is integer division (Python's
   `/` is always float). Use float literals (`7.0 / 2`) for float division.

8. **Containers map to STL types.** `list[T]` → `std::vector<T>`, `dict[K,V]` →
   `std::unordered_map<K,V>`, `array[T,N]` → `std::array<T,N>` (static). List/dict
   **literals** use C++ CTAD to infer element types, so they must be **non-empty**
   (an empty `[]`/`{}` needs a type annotation). Iterating a `dict` yields
   key/value **pairs** (not keys like Python).

9. **Statically typed under the hood (type inference).** `let`/params use C++
   `auto`, so a variable's type is fixed by its initializer. There's no dynamic
   re-typing (`x = 1; x = "s"` won't work).

10. **No indentation significance.** Newlines separate statements; `{ }` groups
    them. Indentation is purely cosmetic. A **`;`** is an *optional* statement
    separator/terminator (`let a = 1; let b = 2`, a trailing `x = x + 1;`, or to
    separate `struct` fields) — handy if you're used to Python's `;` or C++'s, but
    never required.

11. **Exceptions are message-based & catch-all.** `try { … } except e { … }`
    catches any error and binds `e` to the **message string** (not an exception
    object). `raise "msg"` throws a generic error. No typed `except ValueError`,
    `as`, or `finally` yet.

---

## 🚧 Not yet supported (roadmap)

Comprehensions; typed exceptions & `finally`; classes with methods/inheritance;
f-strings & rich string formatting (use `io.format`); slicing (`a[1:3]`); empty
container literals; tuples/unpacking; `with` statements; generators/`yield`;
keyword/default arguments; `lambda`.

These are tracked toward the goal of frictionless Python → cheatah porting.

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
