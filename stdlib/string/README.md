# cheatah `string` 🐆

Text operations plus Python's `string` constants, exposed as free functions so a
`.purr` program writes `string.upper("x")`.

```python
import string

print(string.upper("purr"))            # PURR
print(string.split("a,b,c", ","))      # ['a', 'b', 'c']
print(string.join("-", ["a", "b"]))    # a-b
```

## What's here

- **Constants** — `ascii_lowercase`, `ascii_uppercase`, `ascii_letters`, `digits`,
  `hexdigits`, `octdigits`, `punctuation`, `whitespace`.
- **Case** — `upper`, `lower`, `capitalize`, `title`, `swapcase`.
- **Trimming** — `strip`, `lstrip`, `rstrip`.
- **Search / test** — `startswith`, `endswith`, `contains`, `find`, `rfind`, `count`.
- **Transform** — `replace`, `split`, `splitlines`, `capwords`, `join`.
- **Padding** — `ljust`, `rjust`, `center`, `zfill`.
- **Classification** — `isdigit`, `isalpha`, `isalnum`, `isspace`, `isupper`, `islower`.

Functions returning `std::string` / `std::vector<std::string>` allocate their
result on the heap; the predicate/index functions (`bool`/`long`) do not.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[string.hpp](string.hpp). Tested in [../tests/string_test.cpp](../tests/string_test.cpp);
ASan + Valgrind clean via the QA gate (`security/run-valgrind.sh`).
