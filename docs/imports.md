# Imports & module resolution {#imports}

<div class="cheetah-slogan">🐱 <em>import looks like Python; underneath it resolves to a real file — and verifies it.</em> 🐆</div>

An `import` names a module; `purrc` turns that name into an actual header (and, when
there is one, an archive) on disk. This page is the complete map: the **file layouts**
that work, the exact **order** purrc searches, and the **flags** that change it.

> **This page is optional — skip it on a first read.** If you use the
> [biome](biome.html) package manager, it does *all* of this for you: from your
> `cheatah.toml` it passes the right `--import-root`s and sets the module path, so
> `import` "just works" without you knowing any of the rules below. Come back here only
> when you want to resolve modules by hand, or to understand what biome is doing under
> the hood. This page is the ground truth underneath that convenience.

## A module is a header named after it

purrc recognizes a module by finding a `<m>.hpp`, in one of two shapes:

| Shape | On disk |
|---|---|
| **sibling file** | `<root>/<m>.hpp` |
| **package directory** | `<root>/<m>/<m>.hpp` |

If a `<m>.hpp.sha512` sidecar sits next to the header, purrc treats it as a **signed
cheatah library** and verifies it before use ([below](#verification)). No sidecar → a
plain local/external header; its compiled definitions, if any, are the build's job.

## File layouts that work

Each example shows the layout, the `import`, and the command.

**1 — sibling file (zero config).** Drop a header next to your source:

```
myproj/
├── main.purr      # import helpers
└── helpers.hpp
```
```sh
purrc myproj/main.purr -o app.so      # import helpers → myproj/helpers.hpp
```

**2 — package directory.** A module as its own folder:

```
myproj/
├── main.purr      # import mathx
└── mathx/
    └── mathx.hpp
```
```sh
purrc myproj/main.purr -o app.so      # import mathx → myproj/mathx/mathx.hpp
```

**3 — a dependency elsewhere on disk (`--import-root`).** Point purrc at each root:

```
workspace/
├── app/
│   └── main.purr        # import mathx.linear
└── mathx/
    └── mathx/
        └── mathx.hpp
```
```sh
purrc app/main.purr -o app.so --import-root ../mathx
#   import mathx.linear → ../mathx/mathx/mathx.hpp  ( .linear is a nested namespace )
```

**4 — a signed extension on the module path (`$CHEATAH_MODULE_PATH`).** How fetched
extensions resolve; the header **must** carry a `.sha512` sidecar to match here:

```sh
CHEATAH_MODULE_PATH=/opt/cheatah-ext purrc main.purr -o app.so
#   import mathx → /opt/cheatah-ext/mathx/mathx.hpp   (only if mathx.hpp.sha512 is present)
```

**5 — the first-party stdlib (baked in).** No flags, ever — `io`, `math`, `os`, … are
found at the toolchain root that CMake baked into purrc:

```purr
import io          # baked stdlib root
import os.path     # resolves the `os` module; `.path` is a nested namespace
```

## The resolution order

For a module name `m`, purrc searches these in order and **stops at the first hit**:

| # | Where it looks | Matches | Needs a `.sha512` sidecar? |
|---|---|---|---|
| 1 | The **source file's own directory**, then each **`--import-root <dir>`** (in order given) | `<root>/<m>/<m>.hpp`, then `<root>/<m>.hpp` | No — plain header *or* signed library |
| 2 | **`$CHEATAH_MODULE_PATH`** entries (`:`-separated), then the baked root | `<dir>/<m>/<m>.hpp` **only if** `<dir>/<m>/<m>.hpp.sha512` also exists | Yes — signed libraries only |
| 3 | The **baked toolchain root** (fallback) | `<root>/<m>/<m>.hpp`, archive under the baked lib dir | No — this is how the C++ stdlib resolves |

Consequences worth internalizing:

- **Local wins.** A module next to your source or under `--import-root` is found before
  the env path or the baked stdlib. The source's own directory is always searched first,
  so two files side by side just work (Python-style).
- **Dotted names resolve by their *first* segment.** `import a.b.c` looks up **`a`** as
  the module; `b.c` are nested namespaces *inside* `a`'s header — purrc never looks for a
  file at `a/b/c`. (`import os.path` → the `os` module; `os.path.join(…)` reaches inside it.)
- A co-located **`libcheatah_<m>.a`** is linked if present but never required (branches 1 & 3).
- Resolve nowhere → a clear error naming the unresolved import and telling you to place it
  beside the source, pass `--import-root <dir>`, or declare it in `cheatah.toml`.

## The only knobs that change resolution

Resolution is otherwise fixed; exactly three things steer it:

| Knob | Kind | What it changes |
|---|---|---|
| `--import-root <dir>` | flag (repeatable) | Adds `<dir>` to the front-of-line roots (branch 1) — one per dependency. biome passes these for you. |
| `$CHEATAH_MODULE_PATH` | env (`:`-separated) | Extra roots for **signed** libraries, searched ahead of the baked root (branch 2). biome sets it for fetched extensions. |
| `$CHEATAH_TRUST` | env (file path) | Doesn't change *where* purrc looks — it makes a valid Ed25519 signature **required** on any signed library it resolves (strict, fail-closed). |

## Verification — fail-closed on tamper {#verification}

Before compiling against any resolved **signed** library (one with a `.hpp.sha512`), purrc verifies it:

1. **Checksum (always).** It recomputes the SHA-512 of the header (and, for an opaque
   module, the archive) and compares to the sidecar. Any change since build → the build
   stops. Free, unconditional integrity.
2. **Signature (opt-in).** If `$CHEATAH_TRUST` points to a trust list (one 64-hex Ed25519
   key per non-comment line), purrc *also* requires a valid `.sig` from a key in that list.
   A missing signature, an untrusted key, or a bad signature **fails the build**. Unset →
   checksum-only (a tamper is still caught, just not attributed to a key).

Signatures are produced at build time with `purrc --emit-library --sign <keyfile>`
(generate a keypair with `purrc --keygen <prefix>` — see [purrc](purrc.html)). Plain
headers with no sidecar are not verified here; their definitions are the build's job.

## Transparent vs opaque libraries

A cheatah library is a `.purr` compiled with `purrc --emit-library` into an importable
header in `namespace cheatah::<m>`. Two flavors:

| Flavor | Build | `<m>.hpp` holds | Archive |
|---|---|---|---|
| **opaque** (default) | `purrc --emit-library <m>.purr` | public API only | signed `libcheatah_<m>.a` |
| **transparent** | `purrc --emit-library --transparent <m>.purr` | the full generated C++, inlined | none (header-only) |

`--transparent` is what the first-party stdlib uses, so users (and the VS Code
Go-to-Definition) always see the true C++. Either way the `<m>.hpp` ships **committed next
to the `.purr`** with its `.sha512` sidecar — the exact marker branches 1 & 2 use to
recognize and verify it.

## Where biome fits

[biome](biome.html) automates all of the above: from `cheatah.toml` it turns each
`[dependencies]` entry into a `--import-root`, puts each fetched extension on
`$CHEATAH_MODULE_PATH`, and drives the build through CMake — so `import` "just works"
without you managing the search order by hand.
