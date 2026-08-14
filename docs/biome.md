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
  `biome add <pkg>` is `cargo add`, and `biome build` is `cargo build` — the same
  *describe-it-then-build-it* loop. Two differences: the build biome drives underneath is plain
  CMake (not a bespoke build system you have to learn), and there is **no `biome run`** — you run
  the built program with the `cheatah` runtime (see [Running what you built](#running)).
- **If you know Python's pip + venv:** biome is *both*, fused into the build. It gives you the
  **isolation a virtualenv gives** — each project builds **only** against dependencies it
  fetched into its own tree, so there is no global, mutable `site-packages` and none of the
  "which include path won?" ambiguity — but with **no `venv` to create or activate**.
  `cheatah.toml` lists your dependencies the way `requirements.txt` / `pyproject.toml` does;
  biome fetches each one **locally, into this project's own build folder**, and CMake compiles
  against exactly those copies. The isolation is the point (that's the virtual-environment part);
  it's just hermetic-by-construction instead of something you switch on.

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

The key insight is the middle layer is deliberately thin. All biome really does is
(1) regenerate `CMakeLists.txt` from `cheatah.toml` and (2) invoke `cmake` — split across
`biome configure` (the CMake configure, which fetches everything) and `biome build` (the
CMake build). Everything after that — fetching the toolchain, compiling your `.purr`,
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
biome list [dir]      list the extensions used by the project at <dir> (default: .)
biome standards       list the Biome Standards (tested-together component sets)
biome configure       regenerate CMakeLists.txt from the manifest and run the CMake configure
biome build           build the configured project (add --clean-first for a from-scratch rebuild)
biome version         print the biome version   (also --version / -v)
biome help            usage                      (also --help / -h)
```

That is the **complete** command surface. A few things to note:

- **There is no `biome run`.** biome *builds*; it never runs your program. A cheatah program
  is a loadable module, and you run it with the `cheatah` runtime — see
  [Running what you built](#running).
- **Configure and build are separate** (as in plain CMake). `biome configure` runs the CMake
  configure (CPM fetches the toolchain + extensions); `biome build` compiles what was
  configured. `biome build --clean-first` maps to `cmake --build build --clean-first` (a clean
  rebuild). `--clean-first` is the one flag biome adds.
- **`biome list` is project-scoped.** It shows the extensions of a *specific* project and does
  nothing unless it is pointed at a directory that holds a `cheatah.toml` (default: the current
  directory) — biome is a per-project environment, not a global registry browser.
- Everything else configurable lives in `cheatah.toml`, not on the command line. Anything
  beyond these (a specific CMake generator, a build type, extra compile flags) you set on the
  generated CMake directly — see [Working with plain CMake](#plain-cmake).

## Quick start

```sh
biome init hello
cd hello
biome add cheatah-gpu          # optional — opt into an extension
biome configure                # CMake configure: CPM fetches the toolchain + extensions
biome build                    # compile (add --clean-first to rebuild from scratch)
cheatah build/hello.so         # run it — always with the cheatah runtime, never biome
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
standard = "0.6.0-alpha"    # the Biome Standard — ONE version pinning the whole tested set

[extensions]
cheatah-gpu = "v0.5.0-alpha"      # one line per opted-in extension; the value is the
                                  # release tag RESOLVED from the standard (informational)

[dependencies]
shared = { path = "../shared" }   # a local dependency at a path on disk
```

- `[project] name` names the executable that gets built.
- `[cheatah] standard` pins the **Biome Standard** — the one version you track. It names
  the exact toolchain *and* extension releases that were tested to work together (see
  [The Biome Standard](#biome-standard) below); biome resolves every git tag it writes
  into the generated CMake from it.
- `[cheatah] version` (optional) is a manual override of the *toolchain* tag alone, for
  the rare case you need a toolchain release different from the standard's. Extensions
  always resolve from the standard. Omit it — the standard covers everything.
- `[extensions]` lists optional standard-library extensions (see below). `biome add`
  and `biome remove` edit this section for you; the values are the tags the standard
  resolved, written out so the manifest is self-describing.
- `[dependencies]` lists your own libraries. Each is `name = { path = "…" }` — a
  folder on disk that holds cheatah modules you want to `import`. This is how one
  cheatah project consumes another.

## The Biome Standard {#biome-standard}

The cheatah ecosystem is deliberately many small repositories — you install only what you
need and keep your dependency surface low. What keeps that from becoming version chaos is
the **Biome Standard**: one semantic version that names a *set* of component releases
(the toolchain plus extensions) **tested to work together**. Members keep their own
per-repo versions for internal bookkeeping; the standard is the compatibility contract
*you* rely on, and the one number your manifest pins.

Each standard is a short, append-only definition — the canonical files live in
[`standards/`](https://github.com/BrofessorDoucette/cheatah/tree/main/standards) and
`biome standards` prints the table your biome ships with:

```toml
[standard]
version = "0.6.0-alpha"
released = "2026-08-13"
status = "current"          # current | supported | deprecated (security-only)

[components]
cheatah = "v1.10.0-alpha"
cheatah-gpu = "v0.5.0-alpha"
```

### What the standard's version means

The standard's **major version is a promise about your code**, not about member churn:

- **Major** — only when programs written against the previous standard cannot carry
  forward. Exactly two events justify it: a security measure so necessary we force users
  to change their code, or a change to the cheatah language so fundamental that the
  packages' APIs all had to change — it's basically not the same cheatah anymore. A member
  breaking compatibility with *another member*, absorbed inside the ecosystem so your
  programs still work, is **not** a major.
- **Minor** — the tested set changed in compatible ways: members added features, a member
  took an internal/major bump the ecosystem absorbed, or a new member joined.
- **Patch** — patch-only member updates (fixes, no API change).
- The standard carries `-alpha` while its members are alpha; it earns `1.0.0` only once
  the ecosystem is stable enough that we expect no forced user-code breaks. Standard
  majors are meant to be rare, heartbreaking-level events.

### Worked examples

1. *`cheatah-gpu` fixes a Metal dispatch bug; no API changes anywhere.* cheatah-gpu cuts a
   patch tag; a new standard **patch** (0.1.0 → 0.1.1) pins the fixed set. Your manifest
   bumps one number and nothing about your code changes.
2. *The stdlib adds a new module and `cheatah-gpu` adds new dispatch helpers.* Purely
   additive; existing programs untouched → standard **minor** (0.1.x → 0.2.0).
3. *`cheatah-plot` passes the cross-member gate for the first time and joins the tested
   set.* New member, nothing else changed → standard **minor**.
4. *`cheatah-gpu` reworks the integration seam `cheatah-plot` renders through — a breaking
   change between two members.* Both members are updated and re-tested together inside the
   same new standard; programs written against the documented surfaces keep compiling →
   standard **minor**, explicitly **not** a major. Member-to-member breakage the ecosystem
   absorbs never escalates to the standard's major.
5. *A cryptographic flaw forces removing an insecure API mode that user programs call, and
   it cannot be shimmed safely.* One of the two events that justify a **major**: the
   affected older standards are marked `deprecated` with a public advisory, and the new
   major documents exactly what user code must change.
6. *A fundamental language change (say, a rework of ownership semantics) ripples through
   every package's API.* The other major event — → standard **major**, with the old majors
   remaining installable and archived.

### How the community avoids majors

These are standing practices, not aspirations:

- **Additive evolution** — new capability arrives as new modules/functions beside the old,
  never by mutating existing signatures (sized integers `i8`…`u64` shipped opt-in while
  `int` stayed the 64-bit default).
- **Deprecate, don't delete** — a superseded API is documented deprecated and keeps
  working for the life of the current major; removal is only ever part of a (rare) major.
- **Absorb breaks inside the standard** — when a member must break an inter-member seam,
  every dependent member is updated and cross-tested in the *same* standard release
  (worked example 4), so users only ever see a minor.
- **Backport security patches** — older `supported` standards receive patched member tags
  rather than forcing users forward; only an unpatchable flaw deprecates a standard
  (worked example 5).
- **The standard is published only when the full cross-member QA passes** — each member's
  own gate plus the biome-install integration check. "Tested together" is the definition,
  not a slogan.
- **Nothing is ever stranded** — every standard's member sources stay obtainable
  (immutable git tags, source tarballs attached to each release, and local archives), so
  even a major never cuts a decades-scale project off from the code it builds on. And
  because the ecosystem does its best to maintain standard compatibility with Doxygen
  documentation generators — the full API contract (`@brief`/`@param`/`@return`/
  `@complexity`/`@alloc`/`@test`) lives *in the source headers*, not only on this site —
  we believe the source code itself is of sufficient quality that users could reproduce
  the documentation sites themselves in a decades-long project, even if we are no longer
  hosting them ourselves.

Older standards stay selectable forever: pin `standard = "<older-ver>"` and biome resolves
that set's tags exactly as it did the day it shipped. A standard is only ever retired
(`deprecated`) for a security flaw we could not patch — with a public advisory, and with
its sources still archived.

## Adding an extension — what actually happens

Extensions are optional standard-library packages, each a **separate git repository**
fetched on demand, so you build only what you opt into:

| extension | what it adds |
|---|---|
| `cheatah-gpu` | GPU arrays and compute kernels |
| `cheatah-plot` | Plotting and charting |
| `cheatah-space` | Astronomy and spatial math |

For what each extension provides and — importantly — **which ones depend on *other*
extensions** (not just the standard library), see
[Standard library extensions](extensions.html).

Not every registered extension can be added to every project: `biome add` only accepts
extensions that are **members of your project's Biome Standard** — i.e. releases that were
actually tested with your toolchain. `biome list` shows each extension's resolved tag, or
`(not in this standard)` for ones that have not joined your standard yet. (When an
extension that depends on another joins a standard — `cheatah-plot` builds on
`cheatah-gpu` — the standard carries both members at tested-together tags, which is how
the dependency chain stays coherent.)

Running `biome add cheatah-gpu` does three small things:

1. adds `cheatah-gpu = "v0.5.0-alpha"` (the tag resolved from your standard) to the
   `[extensions]` section of `cheatah.toml`;
2. regenerates `CMakeLists.txt` so it now contains a `CPMAddPackage(NAME cheatah-gpu …)`
   line and passes `cheatah-gpu` to `cheatah_add_program(… EXTENSIONS …)`;
3. prints a reminder to `biome configure` + `biome build`.

The fetch happens on the next `biome configure`, and the compile on `biome build` — not on
`add`, which only edits the manifest and the generated CMake.

## What happens on disk: configure, then build

The two steps map exactly onto CMake's own two phases:

- **`biome configure`** regenerates `CMakeLists.txt` from the manifest, then runs
  `cmake -S . -B build` (the CMake *configure*). During that configure, the generated
  `CMakeLists.txt`:
  1. `include(cmake/CPM.cmake)` — bootstraps CPM (downloaded once into the build tree);
  2. `CPMAddPackage(NAME cheatah … GIT_TAG v1.7.0-alpha)` — **fetches** the cheatah toolchain
     (`purrc`, the runtime, and the stdlib), pinned to the tag your Biome Standard resolves;
  3. one `CPMAddPackage(NAME cheatah-… …)` per extension you opted into;
  4. `include(${cheatah_SOURCE_DIR}/cmake/CheatahProgram.cmake)` then
     `cheatah_add_program(hello SOURCES src/main.purr EXTENSIONS … IMPORT_ROOTS …)`.
- **`biome build`** runs `cmake --build build` (the CMake *build*) — this is where
  `cheatah_add_program` actually compiles. Add `--clean-first` for a from-scratch rebuild.

That helper, `cheatah_add_program`, is where your program is built. It:

- runs **`purrc`** on `src/main.purr` to transpile and compile it into a loadable
  **module** (`build/hello.so`) — purrc never emits a standalone executable;
- points purrc at your dependencies. Each fetched **extension**'s source directory is
  put on purrc's module search path via the `CHEATAH_MODULE_PATH` environment
  variable, and each `[dependencies]` path is passed as a **`--import-root <dir>`**
  flag, so every `import` in your code resolves — and, crucially, it can only resolve
  against what biome fetched into this project's tree (the hermetic, virtual-environment
  guarantee: no ambient global include paths leak in).

## Running what you built {#running}

**biome never runs your program.** A cheatah program is a loadable **module**, and modules
are run by the **`cheatah` runtime** — explicitly:

```sh
cheatah build/hello.so        # this is how you run it — always
cheatah build/hello.so a b c  # extra args arrive as sys.argv
```

This is deliberate: whatever biome selected and built, *you* invoke it by name with the
runtime, so it is never ambiguous which binary or which toolchain actually executes. There is
no `biome run` and no hidden launcher step to reason about — `cheatah <module>.so`, every time.
(See [The cheatah runtime](runtime.html) for what the runtime checks before it loads a module.)

> **Where this is heading.** The version in `cheatah.toml` is meant to pin an exact,
> backed-up cheatah release, and `biome configure` will **install that release's toolchain into
> the project itself** — both the **`purrc` compiler** *and* the **`cheatah` runtime**, placed
> right next to `cheatah.toml`. biome would then always drive *that* co-located `purrc` to
> build, and you would run with *that* co-located `cheatah` — never an ambient one from your
> `PATH` — so it is unambiguous exactly which compiler and which runtime a project uses (the
> virtual-environment guarantee, extended to the toolchain itself: one pinned, project-local
> `purrc`+`cheatah` pair). That install step is still on the roadmap; today the compiler and
> runtime come from the toolchain CPM fetched into `build/`, and biome invokes `purrc` through
> CMake's `cheatah_add_program`.

For the full, exact rules of *how* `purrc` turns each `import` into a file on disk —
the search order, the `--import-root` flag, dotted submodules like `import os.path`,
and how signed modules are verified — see [imports](imports.html). biome is simply
the tool that fills in those import roots for you from your manifest, so you rarely
have to think about them.

## Working with plain CMake {#plain-cmake}

Because the output is ordinary CMake, biome fits into existing C++ projects. Anything
CPM can fetch works: a pinned tag, a branch, or a local checkout of the toolchain via
`-DCPM_cheatah_SOURCE=/path/to/cheatah` (handy while developing the toolchain itself).
A cheatah pulled in as a sub-project builds without its own test suite — tests turn on
only when cheatah is the top-level project.

When you hand-write the CMake instead of letting biome generate it, you take over the job
biome was doing for `import` resolution: passing each dependency as a `--import-root` and
putting fetched extensions on `CHEATAH_MODULE_PATH`. The exact rules purrc follows to turn
an `import` into a file on disk are on the
[Imports & module resolution](imports.html) page.

## Status

This is an early, working tool. Verified end-to-end today: `init` / `add` / `remove` /
`list` / `standards` / `version`, manifest round-tripping, `CMakeLists.txt` generation
with every tag resolved from the Biome Standard, and `build` / `run` against the cheatah
**core** (configure → `purrc` module → native launcher, driven entirely by CMake/CPM).
Biome Standard 0.5.0-alpha has **five members**, every one with a published release tag:
the cheatah toolchain (`v1.11.0-alpha`), `cheatah-gpu` (`v0.5.0-alpha`),
`cheatah-gpu-linalg` (`v0.4.0-alpha`), `cheatah-plot` (`v0.1.0-alpha`), and
`cheatah-space` (`v0.1.0-alpha`). From 0.5.0-alpha on, every standard releases a WORKING
COMBINATION of all five — as members update, the standard's version moves with them
(semantically: additive member updates are minors; a major happens only when user
programs cannot carry forward). `scripts/test-standard-e2e.sh` proves each standard from
an empty directory against the real GitHub tags.

## From an empty directory to a plot

The whole standard, exactly as a new user meets it — no checkouts, no environment, just a
built `biome`:

```sh
mkdir demo && cd demo
biome init proj && cd proj
biome add cheatah-gpu cheatah-gpu-linalg cheatah-plot cheatah-space
```

Put this in `src/main.purr`:

```cheatah
import ndarray
import plot
import plot.figure as figure

let xs = ndarray.array([0.0, 1.0, 2.0, 3.0])
let ys = ndarray.array([0.0, 1.0, 4.0, 9.0])
let fig = figure.line(figure.new_figure(), xs, ys)
fig = figure.title(fig, "hello, standard")
plot.save(fig, "hello.png")
```

```sh
biome configure   # CPM fetches every member from GitHub by the standard's tags
biome build
cheatah build/…/proj.so    # writes hello.png — rendered by cheatah-plot's own rasterizer
```

That flow IS the release acceptance test (`scripts/test-standard-e2e.sh` runs it with every
member exercised and the PNG bytes verified), so if a standard is released, this works.
