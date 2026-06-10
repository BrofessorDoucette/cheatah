# cheatah extension template

The shape of an optional standard-library extension repository (e.g. `cheatah-gpu`,
`cheatah-plot`, `cheatah-space`, `cheatah-learn`). An extension is a **separate git repo**
that `biome` lists in the registry and CPM fetches when a project opts in
(`biome add cheatah-…`).

An extension ships one or more cheatah stdlib **modules** authored in **pure cheatah**
(`.purr`) — the same way the in-tree pure-cheatah modules (e.g. `parsers`) are built. `purrc`
transpiles the `.purr` to templated C++ with concepts and emits a **signed, importable
module**, so a program can `import <module>` and `purrc` verifies and links it.

```
cheatah-example/
├── CMakeLists.txt          # fetches the toolchain, emits the module via cheatah_add_module
├── README.md
└── example/
    ├── example.purr        # the module source (pure cheatah)
    └── example.hpp         # GENERATED + committed by purrc --emit-library (do not edit)
```

## The contract

- A module is authored as `<name>/<name>.purr` and emitted by
  `cheatah_add_module(<name> SOURCES <name>/<name>.purr)`. purrc lowers it into
  `namespace cheatah::<name>`, writes the importable `<name>/<name>.hpp`, and signs it with a
  SHA-512 checksum sidecar (`<name>.hpp.sha512`) — so `import <name>` resolves it and the
  consuming program's purrc **verifies it is unchanged before compiling** against it.
- **Emit mode** (a flag on `cheatah_add_module`):
  - **Opaque** (the default): the header ships only the public API; concretely-typed
    implementations are compiled into — and hidden inside — a signed `libcheatah_<name>.a`.
    Reach for this to distribute a binary extension without source.
  - **Transparent** (`TRANSPARENT`): the generated C++ source is inlined into the committed
    header, so the true code is always visible (what the first-party standard library uses and
    what lets the VS Code extension resolve into it).
  - A **templated** function (untyped params) is header-visible in *both* modes — that is how
    C++ templates work. Opacity hides only **concretely-typed** code; give a function explicit
    param/return types to compile it into the (hidden) archive. *(Full source-hiding of typed
    exports is the active increment.)*
- **Everything the module exposes lives in `namespace cheatah::<name>`.** When a program
  `import`s it, purrc gives it its own short alias (`namespace <name> = ::cheatah::<name>;`)
  inside the generated `cheatah_program` wrapper, so the body reads `<name>::fn(…)`. Two
  extensions must therefore have distinct names.
- Follow the house rules: a per-module `README.md`, Doxygen on the public API, and tests.

See [example/example.purr](example/example.purr) for a minimal module and
[CMakeLists.txt](CMakeLists.txt) for how the extension consumes the toolchain.

> **Consuming an extension from a downstream project:** `cheatah_add_program(… EXTENSIONS
> cheatah-example)` fetches the extension via CPM and adds its module directory to the
> `CHEATAH_MODULE_PATH` purrc searches, so `import example` resolves, verifies, and links it.
> (Authoring a transparent in-tree module — like `parsers` — needs no fetch.)
