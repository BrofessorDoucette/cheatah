# biome — the cheatah package manager

> Both pythons and cheetahs roam the African **savanna**. `biome` is where a
> cheatah project and its (Python-like) dependencies live together.

`biome` scaffolds and builds cheatah projects and lets you opt into optional
standard-library extensions (`cheatah-gpu`, `cheatah-plot`, `cheatah-space`,
`cheatah-learn`, …). It is a thin, declarative layer over **CMake + [CPM](https://github.com/cpm-cmake/CPM.cmake)**:
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
biome list            list available extensions (* = used by this project)
biome build           regenerate CMakeLists.txt from the manifest and build via cmake
biome run             build, then run the produced executable
biome version         print the biome version   (also --version / -v)
biome help            usage                      (also --help / -h)
```

## Quick start

```sh
biome init hello
cd hello
biome add cheatah-plot      # optional — opt into an extension
biome run                   # configures + builds via cmake, then runs ./build/hello
```

A fresh project looks like:

```
hello/
├── cheatah.toml            # the manifest (project name, cheatah version, extensions)
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
version = "0.9.0"           # the cheatah toolchain version, pinned as a git tag

[extensions]
cheatah-plot = "0.1.0"      # one line per opted-in extension
```

`biome add`/`remove` edit `[extensions]` (and regenerate `CMakeLists.txt` so the
two never drift). `biome build` regenerates `CMakeLists.txt` from the manifest
before configuring, so the manifest is always the single source of truth.

## How a project builds (the CMake/CPM flow)

The generated `CMakeLists.txt`:

1. `include(cmake/CPM.cmake)` — bootstraps CPM (downloads it once, into the build
   tree).
2. `CPMAddPackage(NAME cheatah …)` — fetches and builds the cheatah toolchain
   (`purrc`, the runtime, and the stdlib).
3. one `CPMAddPackage(NAME cheatah-… …)` per opted-in extension.
4. `include(${cheatah_SOURCE_DIR}/cmake/CheatahProgram.cmake)` then
   `cheatah_add_program(<name> SOURCES src/main.purr EXTENSIONS …)` — compiles your
   `.purr` into a module with `purrc` and builds a native launcher `<name>` that
   runs it via the cheatah runtime.

This integrates with existing CMake projects: anything CPM can fetch (including a
pinned tag, a branch, or a local checkout via `-DCPM_cheatah_SOURCE=/path`) works,
and a cheatah pulled in as a sub-project builds **without** its own test suite
(tests default on only when cheatah is the top-level project).

## Optional standard-library extensions

Extensions are **separate git repositories**, fetched on demand by CPM — you only
build what you opt into. The registry biome validates against:

| extension | what it adds |
|---|---|
| `cheatah-gpu` | GPU arrays and compute kernels |
| `cheatah-plot` | Plotting and charting |
| `cheatah-space` | Astronomy and spatial math |
| `cheatah-learn` | Machine learning |

See [extension-template/](extension-template/) for the shape of an extension repo.

## Status

This is the first working skeleton. Implemented and verified end-to-end today:
`init` / `add` / `remove` / `list` / `version`, manifest round-tripping,
`CMakeLists.txt` generation, and `build` / `run` against the cheatah **core**
(configure → `purrc` module → native launcher, driven entirely by CMake/CPM).

Pending next increments:
- **Extension link integration** — `cheatah_add_program(EXTENSIONS …)` records
  the chosen extensions and CPM fetches them, but wiring a third-party module's
  headers/archive into `purrc`'s link line is not done yet (purrc currently
  resolves modules from a single baked toolchain root). The four extension repos
  above are not published yet either.
- A pinned `v0.9.0` (and per-extension) **release tag** so CPM fetches a real tag
  rather than needing the local-source override.
- A proper TOML reader (the current parser handles the subset biome writes).
