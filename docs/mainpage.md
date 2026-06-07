# cheatah 🐆 standard library

<div class="cheetah-slogan">🐱 <em>Programs so fast they purrrrrrrrrrrrr like a kitten.</em> 🐆</div>

**cheatah** is Python for people who care about performance: you write `.purr`
source, compile it with `purrc` (lexer → parser → codegen → C++ → `.so`), and run
it on the headless `cheatah` host. This site documents the **standard library** —
the modules a `.purr` program reaches for.

## How to read these docs 🐾

Every standard-library function carries a Doxygen/Javadoc comment with a consistent
contract, so you always know what a call costs:

- **`@param` / `@return`** — what goes in and comes out.
- **`@note`** — the **runtime complexity** (Big-O) **and** whether the function
  performs a **heap allocation** (`no heap`, `allocates the result`, or
  `allocates a temporary`). Memory behavior is a first-class part of the contract:
  cheatah's whole reason for existing is memory safety.
- **`@test`** — a link to the unit test that exercises the function.

The entire library is verified on every QA-gate run under **AddressSanitizer**
(the `asan` preset) and **Valgrind** (`security/run-valgrind.sh`), with **100% line
and function coverage** of the stdlib.

## Modules 🐆

| Module | What it gives you |
|--------|-------------------|
| `builtins` | Always-available built-ins: `len`, `ord`/`chr`, `hex`/`oct`/`bin`, conversions, `hash`. |
| `string`   | Text ops + Python's `string` constants. |
| `math`     | Scalar math (pure, allocation-free) + `abs`/`min`/`max`/`pow`. |
| `io`        | `print`, `str`/`repr`/`format`, `input`, file I/O. |
| `os`        | Environment, process, and filesystem (`os.path`) helpers. |
| `time`      | Monotonic / wall clocks and sleeping. |
| `datetime`  | Civil date & time values and formatting. |
| `random`    | Pseudo-random numbers and selection. |
| `statistics`| Mean, median, variance, standard deviation. |
| `hashlib`   | SHA-256 digests. |
| `ndarray`   | N-dimensional `double` arrays with broadcasting. |
| `linalg`    | numpy-style linear algebra on `ndarray`, SIMD-accelerated. |
| `socket`    | TCP sockets — a small BSD-socket wrapper (Python-`socket`-flavored). |

Browse the **Files** and **Namespaces** tabs above for the full per-function
reference, or start from a module's header (e.g. `math.hpp`, `linalg/routines.hpp`).

---

<div class="cheetah-slogan">🐆 Built for speed. Guarded for safety. 🐱</div>

<p class="cheetah-colophon">Reference site generated with <a href="https://www.doxygen.org/">Doxygen</a> and <a href="https://github.com/jothepro/doxygen-awesome-css">doxygen-awesome-css</a>.</p>
