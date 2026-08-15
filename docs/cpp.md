# cheatah ↔ C++ {#cpp}

<div class="cheetah-slogan">🐱 <em>It's C++ underneath — so a lot of C++ is just cheatah.</em> 🐆</div>

cheatah transpiles to modern C++ and every value **is** a C++ value (`int` → `long
long`, `str` → `std::string`, `list`/`dict` → `std::vector`/`std::unordered_map`). So a
surprising amount of C++ is available **first-class, in ordinary cheatah syntax** — no
`cpp { … }` escape hatch, and none of the escape hatch's loss of memory safety. This page
is the map of what you get natively, and where the escape hatch is still the door.

## First-class — native cheatah, lowers straight to C++

| C++ construct | In cheatah | Lowers to |
|---|---|---|
| `struct` with fields | `struct Point { x: float, y: float }` | a C++ `struct` (a value type) |
| Methods on a record | `fn area(self) { … }` inside the struct | a member function |
| Aggregate construction | `Point({.x = 1})` | a **C++20 designated initializer** (omitted fields zero-init) |
| `enum class` | `enum Color { RED, GREEN }` | a scoped, printable `enum class` |
| Concepts (interfaces) | `interface Drawable { fn draw(self) }` + `struct S: Drawable` | a C++20 **concept** + a `static_assert` |
| `constexpr` | `constexpr fn f() { … }`, `constexpr let x = …` | `constexpr` functions/values; `if`/`match` over them become `if constexpr` |
| Value semantics + RAII | `let s = "hi"`, `with io.open(p) as f { … }` | `std::string`/containers + owning guards that free by scope |
| Generics | `list[T]`, `dict[K, V]`, `ndarray[T]`, interface-constrained params | template instantiations, concept-constrained |
| Operators | `+ - * / // ** and or not ==` … | the matching C++ operators (`**` = power, `//` = floor-div) |
| Slicing / indexing | `xs[i]`, `xs[i:j]`, negative indices | bounds-checked element/subrange access |
| The whole standard library | `import ndarray`, `import tls`, … | linking real C++ modules and calling them with method syntax |

Because all of the above lowers to value types, smart pointers, and RAII, **it stays
memory-safe** — the compiler emits no raw `new`/`delete` for any of it (see
[Security](security.html)).

## Deliberately different from C++

- **No raw pointers, references, or manual memory** in the surface language — ownership is
  value-and-scope. The owning guards (`io.File`, `socket.Conn`, `tls.Conn`,
  `websocket.Client`) replace hand-managed handles; `with` gives deterministic cleanup.
- **No inheritance.** "is-a" is expressed with **interfaces** (concepts), not base classes —
  records only *implement* contracts; they never derive.
- **Exceptions carry a KIND, not a type** — `except e of "index" { … }` selects on a string kind rather
  than `catch (const T&)`, because there is no inheritance to build an exception hierarchy from. `e` is
  an `Error` with `.kind()` and `.message()`, and prints and compares as its message. `raise "msg"`
  raises kind `"error"`; `raise Error("kind", "msg")` names one; a bare `raise` in a handler re-raises.
  Handlers run in order, `finally` runs on every exit path, and **anything no handler claims keeps
  travelling** rather than being swallowed.
- <b>`//` is floor division and `**` is power</b> (not C++'s comment / no-power); `^` is
  bitwise-xor as in C.

## Still the escape hatch: `cpp { … }`

For genuine C++ that has **no cheatah surface**, drop into a raw block — file scope for
`#include`s/helpers/types, inline inside a function where it can read and write cheatah
locals:

```python
import io
cpp { static long long triple(long long n) { return n * 3; } }   # file scope
fn demo() {
    let acc = 0
    cpp { for (int i = 1; i <= 4; ++i) { acc += i; } }           # inline — sees `acc`
    return acc + triple(2)
}
io.print(demo())                                                  # 16
```

Reach for it for: raw pointers / manual allocation, arbitrary third-party C++ headers and
libraries, lambdas, templates you write by hand, and any other C++ feature not listed
above. (Threads no longer need the escape hatch — `import thread` spawns cheatah `fn`s
natively; see the [threading contract](threading.html).) <b>Memory safety is *not* guaranteed inside `cpp { … }`</b> — a raw block bypasses
the value-semantics guarantees, so lifetimes and undefined behavior are your responsibility,
exactly as in C++. Keep it small and rare; everything around it stays native and safe.
