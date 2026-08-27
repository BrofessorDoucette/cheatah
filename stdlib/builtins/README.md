# cheatah `builtins` 🐆

Python's always-available built-ins — no `import` needed. The compiler
auto-includes this module and resolves bare calls like `len("x")` to
`builtins::len`. (The math-flavored built-ins `abs`/`min`/`max`/`round`/`pow`
live in the [`math`](../math/) module.)

```purr
import io
io.print(len("purr"))        # 4
io.print(chr(65), ord("A"))  # A 65
io.print(hex(255))           # 0xff
io.print(int("42") + 1)      # 43
```

## What's here

- **Length** — `len` (any sized container or string).
- **Characters** — `ord`, `chr`.
- **Base representations** — `hex`, `oct`, `bin`.
- **Repr** — `ascii` (printable-ASCII, escaped, single-quoted).
- **Conversions** — `bool`, `int`, `float`, `str` (string and numeric overloads).
- **Hashing** — `hash`.
- **Growable lists** — `append(list, x)` / `xs.append(x)` (in-place push).
- **Indexing & slicing** — `index(seq, i)` (`seq[i]`; negative indices; a string
  index yields a length-1 string) and `slice(seq, lo, hi)` (`seq[lo:hi]`); the
  compiler lowers `seq[i]`/`seq[i:j]` to these.
- **String predicates** — `startswith`, `endswith`, `contains` (usable as methods:
  `s.startswith("…")`).

Templates here are pinned by a concept (`Sized`, `Number`, `Streamable`), a requires-clause,
or a concrete container parameter, so misuse fails with a named error, not template spam.

Scalar-returning built-ins (`len`/`ord`/`to_bool`/`hash`/…) don't allocate, and
neither does `chr` (one byte fits the small-string buffer); `hex`/`oct`/`bin`/`ascii`
allocate their result, and the string-parsing `to_int`/`to_float` build a temporary
`std::string`.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[builtins.hpp](builtins.hpp). Tested in [../tests/builtins_test.cpp](../tests/builtins_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
