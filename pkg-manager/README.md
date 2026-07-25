# biome — the cheatah package manager

> Both pythons and cheetahs roam the African **savanna**. `biome` is where a
> cheatah project and its (Python-like) dependencies live together.

`biome` scaffolds and builds cheatah projects and lets you opt into optional
standard-library extensions (`cheatah-gpu`, `cheatah-plot`, `cheatah-space`, …).
It is a thin, declarative layer over **CMake + [CPM](https://github.com/cpm-cmake/CPM.cmake)**:
you describe a project in `cheatah.toml`, and biome generates the `CMakeLists.txt`
that pulls in the cheatah toolchain and your chosen extensions — so, ultimately,
**everything is handled by CMake**.

biome is itself a cheatah program ([biome.purr](biome.purr)): purrc compiles it to
a module (`biome.so`) and a tiny native **launcher** named `biome` runs that module
through the cheatah runtime — so you type `biome <command>`, and biome's compiled
code still only ever runs under the runtime. See [cheatah_add_program](../cmake/CheatahProgram.cmake).

## Commands

```
biome init <name>     scaffold a new cheatah project
biome add <ext>       add an optional standard-library extension
biome remove <ext>    remove an extension
biome list [dir]      list the extensions used by the project at <dir> (default: .)
biome standards       list the Biome Standards (tested-together component sets)
biome configure       regenerate CMakeLists.txt from the manifest and run the CMake configure
biome build           build the configured project (add --clean-first for a from-scratch rebuild)
biome version         print the biome version   (also --version / -v)
biome help            usage                      (also --help / -h)
```

biome **builds; it never runs your program.** A cheatah program is a loadable module — run it
with the `cheatah` runtime (`cheatah build/<name>.so`), never via biome. `biome list` is
project-scoped: it shows nothing unless pointed at a directory holding a `cheatah.toml`.

## Quick start

```sh
biome init hello
cd hello
biome add cheatah-gpu          # optional — opt into an extension
biome configure                # CMake configure: CPM fetches the toolchain + extensions
biome build                    # compile (add --clean-first to rebuild from scratch)
cheatah build/hello.so         # run it — always with the cheatah runtime, never biome
```

A fresh project looks like:

```
hello/
├── cheatah.toml            # the manifest (project name, Biome Standard, extensions)
├── CMakeLists.txt          # GENERATED from cheatah.toml — do not edit by hand
├── cmake/
│   └── CPM.cmake           # CPM bootstrap (auto-downloads CPM at configure time)
├── src/
│   └── main.purr           # your program
└── .gitignore
```

## The manifest — `cheatah.toml`

```toml
[project]
name = "hello"

[cheatah]
standard = "0.1.0-alpha"    # the Biome Standard — ONE version pinning the whole tested set

[extensions]
cheatah-gpu = "v0.5.0-alpha"      # per opted-in extension: the tag RESOLVED from the standard

[dependencies]
shared = { path = "../shared" }   # a local/path dependency (crate-style)
```

`biome add`/`remove` edit `[extensions]` (and regenerate `CMakeLists.txt` so the
two never drift). `biome build` regenerates `CMakeLists.txt` from the manifest
before configuring, so the manifest is always the single source of truth.

The **Biome Standard** is the one version users track: it names the set of component
releases (toolchain + extensions) tested to work together, and biome resolves every
`GIT_TAG` in the generated CMake from it. The canonical definitions live in
[../standards/](../standards/), mirrored by the append-only table in `biome.purr`
(`scripts/check_standards.sh` fails the QA gate if the two drift). An optional
`[cheatah] version = "…"` remains as a manual override of the *toolchain* tag alone.
See the "The Biome Standard" section of `docs/biome.md` for the full versioning
contract (when its major/minor/patch move, and the source-retention guarantee).

## Dependencies & how `import` resolves

cheatah's `import` works like Python's, resolved by the compiler in this order:

1. **Relative to the source** — `import a.b.c` first looks for `a/b/c.purr` (or an
   already-built/C++ `a/b/c.hpp`) **next to the file being compiled**, then in its
   subfolders. Drop modules next to each other and they just work — no config.
2. **Declared path dependencies** — anything not found relative is resolved from the
   `[dependencies]` you declare here. Each `name = { path = "…" }` becomes a
   `--import-root <path>` on the `purrc` invocation (via `cheatah_add_program`'s
   `IMPORT_ROOTS`), so `import name.mod` finds `<path>/name/mod.hpp`. A dependency may be
   a pure-cheatah package or an external C++ library exposing cheatah module headers; any
   compiled library it ships is linked by CMake, **not** the compiler — so there is no
   archive-naming convention to satisfy.
3. **Otherwise** the compiler errors clearly, naming the unresolved `import` and telling
   you to place it beside the source, add it to `[dependencies]`, or pass `--import-root`.

This is what lets an **external** project (one cheatah knows nothing about) consume a
cheatah library by pointing at it — a downstream app can declare its own packages as
path dependencies, and biome wires them through.

## How a project builds (the CMake/CPM flow)

The generated `CMakeLists.txt`:

1. `include(cmake/CPM.cmake)` — bootstraps CPM (downloads it once, into the build
   tree).
2. `CPMAddPackage(NAME cheatah …)` — fetches and builds the cheatah toolchain
   (`purrc`, the runtime, and the stdlib).
3. one `CPMAddPackage(NAME cheatah-… …)` per opted-in extension.
4. `include(${cheatah_SOURCE_DIR}/cmake/CheatahProgram.cmake)` then
   `cheatah_add_program(<name> SOURCES src/main.purr EXTENSIONS … IMPORT_ROOTS …)` —
   compiles your `.purr` into a module with `purrc` (passing each `[dependencies]` path as
   `--import-root`) and builds a native launcher `<name>` that runs it via the cheatah
   runtime.

This integrates with existing CMake projects: anything CPM can fetch (including a
pinned tag, a branch, or a local checkout via `-DCPM_cheatah_SOURCE=/path`) works,
and a cheatah pulled in as a sub-project builds **without** its own test suite
(tests default on only when cheatah is the top-level project).

## Optional standard-library extensions

Extensions are **separate git repositories**, fetched on demand by CPM — you only
build what you opt into. The registry biome validates names against (membership in
your project's Biome Standard decides whether an extension is *addable* — `biome
standards` shows each standard's members):

| extension | what it adds |
|---|---|
| `cheatah-gpu` | GPU arrays and compute kernels |
| `cheatah-plot` | Plotting and charting |
| `cheatah-space` | Astronomy and spatial math |

See [extension-template/](extension-template/) for the shape of an extension repo.

## Status

Implemented and verified end-to-end today: `init` / `add` / `remove` / `list` /
`standards` / `version`, manifest round-tripping, `CMakeLists.txt` generation with
every tag resolved from the Biome Standard, and `build` / `run` against the cheatah
**core** (configure → `purrc` module → native launcher, driven entirely by CMake/CPM).
Biome Standard 0.1.0-alpha members: the cheatah toolchain (`v1.7.0-alpha`) and
`cheatah-gpu` (`v0.5.0-alpha`) — both published tags. `cheatah-plot` and
`cheatah-space` are registered but not yet members of any standard, so `biome add`
declines them until they pass the cross-member gate.

Pending next increments:
- **Extension link integration** — `cheatah_add_program(EXTENSIONS …)` records
  the chosen extensions and CPM fetches them, but wiring a third-party module's
  headers/archive into `purrc`'s link line is not done yet (purrc currently
  resolves modules from a single baked toolchain root).
- `cheatah-plot` / `cheatah-space` release tags + cross-member gate runs, so they
  can join a standard.
- A proper TOML reader (the current parser handles the subset biome writes).
