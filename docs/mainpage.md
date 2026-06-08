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
- **`@complexity`** — the **runtime complexity** (Big-O) of the call.
- **`@alloc`** — the function's **heap behavior**: `none`, or what it allocates
  (`allocates the result`, `allocates a temporary`, scratch buffers, …). Memory
  behavior is a first-class part of the contract: cheatah's whole reason for
  existing is memory safety.
- **`@test`** — a link to the unit test that exercises the function.

> **On `@complexity`:** this is the *algorithmic* (Big-O) cost — how the work
> scales with input size. The actual wall-clock time is **machine-dependent**, and
> the constant factors can also shift with the **C runtime / standard library**
> that gets linked when your program runs. Treat Big-O as the contract; benchmark
> for absolute numbers.

The entire library is verified on every QA-gate run under **AddressSanitizer**
(the `asan` preset) and **Valgrind** (`security/run-valgrind.sh`), with **100% line
and function coverage** of the stdlib.

See the **[Performance](performance.html)** guide for how cheatah delivers
hand-written-C++ speed — zero-cost generic abstractions, declarative SIMD, and the
compiler's automatic string-concatenation optimization — at the cost of compile time.

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
| `ndarray`   | N-dimensional arrays **generic over the numeric element type** (deduced from the literals), with broadcasting + declarative SIMD. |
| `linalg`    | numpy-style linear algebra on `ndarray`, SIMD-accelerated. |
| `socket`    | TCP sockets — a small BSD-socket wrapper (Python-`socket`-flavored). |

Browse the **Files** and **Namespaces** tabs above for the full per-function
reference, or start from a module's header (e.g. `math.hpp`, `linalg/routines.hpp`).

---

<div class="cheetah-slogan">🐆 Built for speed. Guarded for safety. 🐱</div>

<p class="cheetah-colophon">C++ API parsed with <a href="https://www.doxygen.org/">Doxygen</a>; this site is rendered by cheatah's own documentation generator.</p>
