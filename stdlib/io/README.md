# cheatah `io`

Python-like input/output, surfaced as free functions plus a Python-style `File`
object. Mirrors the [Python I/O tutorial](https://docs.python.org/3/tutorial/inputoutput.html).

## Usage

```purr
import io

io.print("meow", 42, "purr")            # -> meow 42 purr
s = io.format("{} ate {} fish", "cat", 3)  # -> "cat ate 3 fish"

f = io.open("notes.txt", "w")
f.write("hello\n")
f.close()
```

`import io` includes `io.hpp` and links `libcheatah_io`; a program that doesn't
import it neither sees nor links it.

## Functions

### Rendering
- `str(x)` — stringify any streamable value (bool → `True`/`False`).
- `repr(x)` — like `str`, but strings are single-quoted.
- `format(fmt, ...)` — sequential `{}` substitution (str.format / f-string style).

### Console
- `print(*args)` — space-separated, newline-terminated, to stdout.
- `input(prompt="")` — write the prompt, read one line from stdin.

### Files
- `open(path, mode="r")` — returns a `File` (Python modes `r`/`w`/`a`, `+`/`b`).
- `File::read()` — the whole remaining file.
- `File::readline()` — the next line (newline stripped; `""` at EOF).
- `File::readlines()` — all remaining lines as a vector.
- `File::write(value)` — write a streamable value.
- `File::is_open()` / `File::close()` — handle state; RAII closes on scope exit.

Per-function docs (parameters, complexity, heap behavior) are in [io.hpp](io.hpp).
Tested in [../tests/io_test.cpp](../tests/io_test.cpp); ASan + Valgrind clean via
the QA gate (`security/run-valgrind.sh`).
