# cheatah extension template

The shape of an optional standard-library extension repository (e.g.
`cheatah-gpu`, `cheatah-plot`, `cheatah-space`, `cheatah-learn`). An extension is
a **separate git repo** that `biome` lists in the registry and CPM fetches when a
project opts in (`biome add cheatah-…`).

An extension ships one or more cheatah stdlib **modules** — built exactly like the
in-tree modules (`io`, `os`, `ndarray`, …) via cheatah's `add_cheatah_library`
helper, so a program can `import <module>` and `purrc` links the archive.

```
cheatah-example/
├── CMakeLists.txt          # fetches the cheatah toolchain, builds the module(s)
├── README.md
└── example/
    ├── example.hpp         # namespace cheatah::example { … }  (#pragma once)
    └── example.cpp
```

## The contract

- A module lives in `namespace cheatah::<name>` with a `<name>.hpp` (the public
  include dir is the module directory, so `import <name>` finds `<name>.hpp`) and
  is built as `libcheatah_<name>.{a,so}` by `add_cheatah_library(<name> …)`.
- **Put _everything_ the module exposes inside `namespace cheatah::<name>`** — and
  nothing in the global namespace. When a program `import`s your module, `purrc`
  gives it its OWN short alias and calls it through that: it wraps the generated
  program in a `namespace cheatah_program` and emits
  `namespace <name> = ::cheatah::<name>;`, so the body reads `<name>::fn(…)`. Each
  module gets a DISTINCT alias (so two extensions must have distinct names), and the
  alias can never collide with a global C symbol of the same name (e.g. a module
  named `time`/`random`/`socket`) precisely because it resolves to `::cheatah::<name>`
  and lives inside the wrapper namespace. Anything you leave OUTSIDE
  `cheatah::<name>` (a global helper, a symbol in a different namespace) is NOT
  reachable through the alias.
- Expose **flat free functions** (cheatah has no user-facing method calls on
  module values yet) over cheatah's value types — `long long` ints,
  `std::string`, `std::vector`, `std::unordered_map`.
- Follow the house rules: unit tests, a per-module `README.md`, Doxygen with
  `@complexity`/`@alloc`/`@test`, and the ASan/UBSan/Valgrind QA gate.

See [example/example.hpp](example/example.hpp) for a minimal module, and
[CMakeLists.txt](CMakeLists.txt) for how the extension consumes the toolchain.

> **Note:** wiring a third-party extension module into `purrc`'s link line from a
> downstream project is the next biome increment — today `cheatah_add_program`
> records and fetches the extensions but does not yet add their archives to the
> link. This template documents the intended structure.
