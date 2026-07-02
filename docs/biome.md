# biome — the package manager {#biome}

<div class="cheetah-slogan">🐱 <em>A friendly face on CMake — you edit one file, biome builds the rest.</em> 🐆</div>

`biome` is cheatah's package manager: it scaffolds a new project, tracks what your
project depends on, and builds it. Whichever tool you already know, biome maps onto it:

- **If you know [CMake](https://cmake.org/):** biome *is* CMake. It generates the
  `CMakeLists.txt` and runs the configure/build for you, with
  **[CPM](https://github.com/cpm-cmake/CPM.cmake)** added to fetch dependencies. You never
  leave CMake — you just stop hand-writing it, and you can drop back to raw `cmake` whenever
  you want.
- **If you know Rust's Cargo:** biome is cheatah's Cargo. `cheatah.toml` is your `Cargo.toml`,
  `biome add <pkg>` is `cargo add`, and `biome run` is `cargo run` — the same
  *describe-it-then-build-it* loop. The one difference: the build biome drives underneath is
  plain CMake, not a bespoke build system you have to learn.
- **If you know Python's pip:** think *project* tool, not global installer. `cheatah.toml`
  lists your dependencies the way `requirements.txt` / `pyproject.toml` does — but instead of
  installing packages into an environment, biome fetches them and compiles one native binary,
  no virtualenv and no runtime resolution.

So biome is **not** a new build system with its own rules: under the hood it is CMake + CPM,
and it writes the CMake for you and presses the buttons.

## What problem biome solves

A cheatah program becomes native code. To build it you need the cheatah toolchain
(the `purrc` compiler, the runtime, and the standard library), and if you use an
optional extension like plotting or GPU arrays you need *that* too — each is a
separate piece of software that has to be downloaded, built, and wired onto the
compiler's search path. Doing that by hand means writing CMake and knowing the flags.

biome removes the hand-work. You list what you want; biome fetches it and builds it.

## The mental model: a thin wrapper over CMake

Think of three layers, top to bottom:

| Layer | What it is | Analogy |
|---|---|---|
| `cheatah.toml` | a short manifest you edit — project name, toolchain version, extensions, dependencies | the "shopping list" |
| **biome** | reads the manifest and **generates** a `CMakeLists.txt`, then runs `cmake` | the cook who reads the list and does the work |
| CMake + CPM | actually downloads dependencies, runs `purrc`, and produces the executable | the kitchen |

The key insight is the middle layer is deliberately thin. `biome build` does two
real things: (1) regenerate `CMakeLists.txt` from `cheatah.toml`, and (2) run
`cmake`. Everything after that — fetching the toolchain, compiling your `.purr`,
linking — is plain CMake. So **anything you already know about CMake still applies**,
and biome never gets in your way; the generated `CMakeLists.txt` is ordinary CMake
you could run yourself.

CPM is the one extra ingredient. It is a single CMake file that adds a
"download this git repository and build it" command (`CPMAddPackage`). That is how
the cheatah toolchain and each extension arrive on your machine — pinned to a
version tag, fetched into your build folder the first time you configure.

## biome is itself a cheatah program

A neat detail: biome is written *in cheatah* ([biome.purr](https://github.com/BrofessorDoucette/cheatah/blob/main/pkg-manager/biome.purr)).
`purrc` compiles it to a loadable module (`biome.so`) and a small native
**launcher** named `biome` runs that module through the cheatah runtime — so when
you type `biome build`, biome's own compiled logic still only ever runs under the
runtime, exactly like any other cheatah program (it is built with the same
`cheatah_add_program` helper your projects use). At runtime biome simply shells out
to `cmake`; it has no build dependency of its own beyond the toolchain it ships with.

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

`biome init hello` writes a ready-to-build project:

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

You edit `src/main.purr` (and, occasionally, `cheatah.toml`). You never edit the
generated `CMakeLists.txt` — `biome build` rewrites it from the manifest every time,
so the manifest is always the single source of truth and the two never drift apart.

## The manifest — `cheatah.toml`

TOML is just a simple `key = value` text format grouped into `[sections]`. A project
manifest looks like this:

```toml
[project]
name = "hello"

[cheatah]
version = "0.9.0"           # the cheatah toolchain version, pinned as a git tag

[extensions]
cheatah-plot = "0.1.0"      # one line per opted-in extension

[dependencies]
learning = { path = "../learning" }   # a local dependency at a path on disk
```

- `[project] name` names the executable that gets built.
- `[cheatah] version` pins which release of the toolchain to fetch (a git tag).
- `[extensions]` lists optional standard-library extensions (see below). `biome add`
  and `biome remove` edit this section for you.
- `[dependencies]` lists your own libraries. Each is `name = { path = "…" }` — a
  folder on disk that holds cheatah modules you want to `import`. This is how one
  cheatah project consumes another.

## Adding an extension — what actually happens

Extensions are optional standard-library packages, each a **separate git repository**
fetched on demand, so you build only what you opt into:

| extension | what it adds |
|---|---|
| `cheatah-gpu` | GPU arrays and compute kernels |
| `cheatah-plot` | Plotting and charting |
| `cheatah-space` | Astronomy and spatial math |
| `cheatah-learn` | Machine learning |

Running `biome add cheatah-plot` does three small things:

1. adds `cheatah-plot = "0.1.0"` to the `[extensions]` section of `cheatah.toml`;
2. regenerates `CMakeLists.txt` so it now contains a `CPMAddPackage(NAME cheatah-plot …)`
   line and passes `cheatah-plot` to `cheatah_add_program(… EXTENSIONS …)`;
3. prints a reminder to `biome build`.

The download and build happen on the next `biome build`, not on `add` — `add` only
edits the manifest and the generated CMake.

## What happens on disk when you build

`biome build` runs `cmake -S . -B build` then `cmake --build build`. During that
CMake run, the generated `CMakeLists.txt` does the following:

1. `include(cmake/CPM.cmake)` — bootstraps CPM (downloads it once into the build tree).
2. `CPMAddPackage(NAME cheatah … GIT_TAG v0.9.0)` — fetches and builds the cheatah
   toolchain (`purrc`, the runtime, and the stdlib).
3. one `CPMAddPackage(NAME cheatah-… …)` per extension you opted into.
4. `include(${cheatah_SOURCE_DIR}/cmake/CheatahProgram.cmake)` then
   `cheatah_add_program(hello SOURCES src/main.purr EXTENSIONS … IMPORT_ROOTS …)`.

That last helper, `cheatah_add_program`, is where your program is actually built. It:

- runs **`purrc`** on `src/main.purr` to transpile and compile it into a loadable
  **module** (`hello.so`) — purrc never emits a standalone executable;
- points purrc at your dependencies. Each fetched **extension**'s source directory is
  put on purrc's module search path via the `CHEATAH_MODULE_PATH` environment
  variable, and each `[dependencies]` path is passed as a **`--import-root <dir>`**
  flag, so every `import` in your code resolves;
- builds a tiny native **launcher** named `hello` that runs the module through the
  cheatah runtime — so you invoke `./build/hello`, and your compiled code still only
  ever runs under the runtime.

`biome run` does all of the above and then executes `./build/hello`.

For the full, exact rules of *how* `purrc` turns each `import` into a file on disk —
the search order, the `--import-root` flag, dotted submodules like `import os.path`,
and how signed modules are verified — see [imports](imports.html). biome is simply
the tool that fills in those import roots for you from your manifest, so you rarely
have to think about them.

## Working with plain CMake

Because the output is ordinary CMake, biome fits into existing C++ projects. Anything
CPM can fetch works: a pinned tag, a branch, or a local checkout of the toolchain via
`-DCPM_cheatah_SOURCE=/path/to/cheatah` (handy while developing the toolchain itself).
A cheatah pulled in as a sub-project builds without its own test suite — tests turn on
only when cheatah is the top-level project.

## Status

This is an early, working skeleton. Verified end-to-end today: `init` / `add` /
`remove` / `list` / `version`, manifest round-tripping, `CMakeLists.txt` generation,
and `build` / `run` against the cheatah **core** (configure → `purrc` module → native
launcher, driven entirely by CMake/CPM). Full extension link-integration and published
extension release tags are still in progress.
</content>
</invoke>
