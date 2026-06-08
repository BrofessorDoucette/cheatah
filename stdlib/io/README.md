# cheatah `io`

Python-like input/output, surfaced as free functions plus a Python-style `File`
object. Mirrors the [Python I/O tutorial](https://docs.python.org/3/tutorial/inputoutput.html).

## Usage

```purr
import io

io.print("meow", 42, "purr")               # -> meow 42 purr
let s = io.format("{} ate {} fish", "cat", 3)  # -> "cat ate 3 fish"

let f = io.open("notes.txt", "w")
f.write("hello\n")
f.close()
```

`import io` includes `io.hpp` and links `libcheatah_io`; a program that doesn't
import it neither sees nor links it.

## Functions

### Rendering
- `str(x)` — stringify any **Printable** value (bool → `True`/`False`; lists →
  `[1, 2, 3]`; dicts → `{'k': 1}`; an object with a `str()` method → its `str()`).
- `repr(x)` — like `str`, but strings are single-quoted (incl. inside lists/dicts).
- `format(fmt, ...)` — sequential `{}` substitution (str.format / f-string style).

### Console
- `print(*args)` — space-separated, newline-terminated, to stdout. Accepts any
  **Printable** arg, not just streamable scalars.

### The `Printable` protocol
`print`/`str` require **`Printable`**, not raw streamability: a value is printable
if it streams directly (numbers, strings, bool), exposes a `str()` method (a struct
that implements `fn str(self)`, or a built-in object like an `ndarray`), or is a
`list`/`dict` whose elements are themselves printable (checked recursively). So you
can `io.print([1, 2, 3])`, `io.print(myStruct)`, and `io.print(someNdarray)` — and
a non-printable type fails with a clear *"does not satisfy `Printable`"* error.

- `input(prompt="")` — write the prompt, read one line from stdin.

### Files
- `read_file(path)` — read a whole file into a string in one call (binary-safe; `""` if it can't be opened).
- `open(path, mode="r")` — returns a `File` (Python modes `r`/`w`/`a`, `+`/`b`).
- `File::read()` — the whole remaining file.
- `File::readline()` — the next line (newline stripped; `""` at EOF).
- `File::readlines()` — all remaining lines as a vector.
- `File::write(value)` — write a streamable value.
- `File::is_open()` / `File::close()` — handle state; RAII closes on scope exit.

Per-function docs (parameters, complexity, heap behavior) are in [io.hpp](io.hpp).
Tested in [../tests/io_test.cpp](../tests/io_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
