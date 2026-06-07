# Changelog

All notable changes to cheatah. This project is **pre-alpha** — expect breaking
changes between releases.

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
