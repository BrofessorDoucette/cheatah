# Changelog

All notable changes to cheatah. This project is **pre-alpha** — expect breaking
changes between releases.

## v0.2.0-alpha — networking, a bespoke docs site, and a three-tier test system

Second pre-alpha. The language core is unchanged; this release adds **networking**
to the standard library, replaces the documentation pipeline with our **own
site generator**, and builds out a **three-tier, per-function test system** behind
a stricter QA gate.

### Standard library
- **New `socket` module** — a thin, memory-safe BSD-socket wrapper
  (`tcp_listen`/`tcp_connect`/`accept`/`send`/`sendall`/`recv`/`bind`/`listen`/
  `connect`/`close`/`local_port`/`last_error`, …). `import socket`.
- A **pure-cheatah docs server** (`scripts/serve-docs.purr`) written entirely in
  `.purr` on top of `socket` — no `cpp { }`, no raw pointers — that serves the
  generated site over HTTP. Proof that real programs can be written in cheatah.

### Documentation
- **Bespoke documentation site.** Doxygen is now used *only* as the C++ parser
  (it emits XML); our own generator (`docs/gen/generate.py`) renders a modern
  static site — left module sidebar, client-side symbol search, a source browser,
  a light/dark toggle, cache-busted assets, and accessible (WCAG 2.1 AA) contrast.
  Replaces doxygen-awesome entirely.
- **Structured doc tags.** The old combined `@note` is split into `@complexity`
  (Big-O) and `@alloc` (heap behavior); every function also links **three test
  kinds** — `@test` (unit), `@crtest` (compile-run), `@systest` (system) — straight
  to their source. **100% Javadoc coverage** of the public stdlib, plus a
  behavioral description for ~190 functions.

### Testing
- **Three-tier, per-function tests:** a C++ **unit** test and a **compile-run**
  test (compile a `.purr` calling the function, run it on the runtime, assert exact
  stdout) for every function; a **comprehensive per-module system** test that
  exercises *every* function of its module; and six **cross-module system apps**
  (GradeReport, LinearSolve, EventLog, Integrity, MonteCarlo, NetworkRoundtrip)
  that only pass if many modules cooperate.

### Quality & security
- The QA gate now **hard-fails** below **100% unit-test line+function coverage** and
  below **100% Javadoc coverage**. ASan/UBSan + Valgrind run across all test tiers.
- The ~200-test per-function compile-run battery is **opt-in** (`QA_GATE_FULL_CR=1`)
  so the default gate stays fast.
- On a passing push to `main`, the docs site is **auto-regenerated**.

## v0.1.0-prealpha — first pre-alpha

The first tagged pre-alpha of the cheatah language: it compiles and runs, with a
standard library, an editor extension, and CI that runs every test under two memory
checkers.

### Language
- Python-like surface, C-style `{ }` blocks, compiles to native code via `purrc`
  (lexer → parser → codegen → C++), run by the headless `cheatah` runtime.
- `let` variables; `int`/`float`/`str`/`bool`; full operators incl. `**` (power);
  `if`/`else if`/`else`, `while`, `for … in range(…)`; `fn` functions (recursion);
  `struct` records; `list`/`dict`/`array` collections; `try`/`except` + `raise`;
  `import` with `as` aliases and dotted modules.
- **`cpp { … }` raw-C++ escape hatch** — file scope at the top level, inline inside
  a function (memory safety is the author's responsibility there).
- **`;`** is an optional statement separator/terminator (and struct-field separator).

### Standard library
`builtins`, `io`, `os`, `string`, `math`, `time`, `datetime`, `random`,
`statistics`, `hashlib`, and a SIMD numeric core (`ndarray` + numpy-style `linalg`).
Each module builds as both a static and shared library.

### Tooling
- VS Code extension ([editors/vscode/](editors/vscode/)): syntax highlighting
  (with embedded C++ in `cpp { … }`) and a "Seti + cheetah" file icon theme.
- `purrc --version` / `cheatah --version`.

### Quality & security
- 100+ tests (unit + purrc→runtime end-to-end). QA gate (pre-push hook) runs them
  under **ASan + UBSan** and **Valgrind**, plus release benchmarks.
- Security review + hardening (ndarray overflow/OOB guards); threat model and the
  Unix-interface/MCP safe-design plan in [SECURITY.md](SECURITY.md).

### Known limitations
- Single-trust model — do **not** run untrusted `.purr` yet (no sandbox).
- No Unix system-call interface or MCP server yet (designed, not built).
- Native GitHub `.purr` highlighting pending a github-linguist submission.
