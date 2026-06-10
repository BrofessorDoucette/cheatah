# cheatah `os`

Python-like operating-system interface, built on `std::filesystem`. Includes the
`os.path` path-manipulation submodule.

```purr
import os

os.getcwd()
os.makedirs("build/cache")
os.path.join("a", "b", "c")          # "a/b/c"
os.path.splitext("dir/file.purr")    # {"dir/file", ".purr"}
```

## Functions

Working directory & process:
- `getcwd()`, `chdir(path)` — read / change the cwd.
- `getpid()`, `cpu_count()`, `system(command)` — process id, logical CPU count,
  run a shell command.
- `urandom(n)` — `n` cryptographically secure random bytes (from the OS CSPRNG).

Directories & files:
- `listdir(path=".")` — entry basenames in a directory.
- `mkdir(path)`, `makedirs(path)` — create one / a directory tree.
- `rmdir(path)`, `remove(path)`, `rename(src, dst)` — remove / move entries.

Environment:
- `getenv(name, fallback="")`, `setenv(name, value, overwrite=true)`.

`os.path` submodule:
- `join(first, ...)` — join components with the platform separator.
- `exists(p)`, `isfile(p)`, `isdir(p)` — path predicates.
- `basename(p)`, `dirname(p)`, `abspath(p)`, `normpath(p)` — path components.
- `getsize(p)` — file size in bytes.
- `splitext(p)` — split into `{root, extension}`.

Per-function docs (parameters, runtime complexity, heap behavior) are in
[os.hpp](os.hpp). Tested in [../tests/os_test.cpp](../tests/os_test.cpp); ASan +
Valgrind clean via the QA gate (`security/run-valgrind.sh`).
