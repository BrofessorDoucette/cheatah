# cheatah `builtins` 🐆

Python's always-available built-ins — no `import` needed. The compiler
auto-includes this module and resolves bare calls like `len("x")` to
`builtins::len`. (The math-flavored built-ins `abs`/`min`/`max`/`round`/`pow`
live in the [`math`](../math/) module.)

```python
print(len("purr"))     # 4
print(chr(65), ord("A"))  # A 65
print(hex(255))        # 0xff
print(to_int("42") + 1)   # 43
```

## What's here

- **Length** — `len` (any sized container or string).
- **Characters** — `ord`, `chr`.
- **Base representations** — `hex`, `oct`, `bin`.
- **Repr** — `ascii` (printable-ASCII, escaped, single-quoted).
- **Conversions** — `to_bool`, `to_int`, `to_float` (string and numeric overloads).
- **Hashing** — `hash`.

Scalar-returning built-ins (`len`/`ord`/`to_bool`/`hash`/…) don't allocate;
the string-building ones (`chr`/`hex`/`oct`/`bin`/`ascii`) allocate their result,
and the string-parsing `to_int`/`to_float` allocate a temporary `std::string`.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[builtins.hpp](builtins.hpp). Tested in [../tests/builtins_test.cpp](../tests/builtins_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
