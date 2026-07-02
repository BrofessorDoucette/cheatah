# Imports & module resolution {#imports}

<div class="cheetah-slogan">🐱 <em>import looks like Python; underneath it resolves to a real file — and verifies it.</em> 🐆</div>

An `import` in cheatah names a module; the compiler, `purrc`, has to turn that name
into an actual header (and, when there is one, an archive) on disk. This page is the
complete map of *how* — every place `purrc` looks, in what order, how dotted names
like `import os.path` resolve, and how a signed cheatah library is verified before
your program is allowed to link it.

If you use the [biome](biome.html) package manager you rarely touch any of this by
hand: biome passes the right `--import-root`s for your dependencies automatically.
This page is the ground truth underneath that convenience.

## What a module *is*

A module is anything that exposes a `<m>.hpp` header. There are two shapes purrc
recognizes:

| Shape | On disk | Example |
|---|---|---|
| **package directory** | `<root>/<m>/<m>.hpp` | `os/os.hpp`, `ndarray/ndarray.hpp` |
| **sibling file** | `<root>/<m>.hpp` | a `helpers.hpp` sitting right next to your `main.purr` |

If a header has a checksum sidecar next to it (`<m>.hpp.sha512`), purrc treats it as
a **cheatah library module** — one it will verify before use (see
[Verification](#verification)). A header with no sidecar is a plain local or external
module: its compiled definitions, if it has any, are the build's job to link, not the
compiler's — so there is no archive-naming convention you must satisfy.

## Dotted names resolve by their first segment

`import a.b.c` does **not** look for a file at `a/b/c`. purrc resolves only the
**first segment** as the module (here, `a`); the remaining segments are nested
namespaces *inside* that module's header, accessed with the same dotted syntax in
your code.

- `import os.path` resolves the **`os`** module (finds `os/os.hpp`); `os.path.join(…)`
  calls into a `path` namespace nested inside it.
- `import parsers.json` resolves the **`parsers`** module (finds `parsers/parsers.hpp`);
  `parsers.json.…` reaches its nested `json` namespace.

So for resolution purposes, "the module" is always the leading name.

## The resolution order

For a module name `m`, `purrc`'s `resolve_module()` searches these locations **in
order** and stops at the first hit:

| # | Where | What it matches | Requires a signed sidecar? |
|---|---|---|---|
| 1 | **The source file's own directory**, then each **`--import-root <dir>`** (in the order given) | `<root>/<m>/<m>.hpp` (package dir), then `<root>/<m>.hpp` (sibling) | No — may be a plain header *or* a signed cheatah library |
| 2 | **`$CHEATAH_MODULE_PATH`** entries (`:`-separated), then the **baked toolchain root** | `<dir>/<m>/<m>.hpp` **only if** `<dir>/<m>/<m>.hpp.sha512` also exists | Yes — this branch only matches signed cheatah library modules |
| 3 | **The baked toolchain root** (fallback) | `<root>/<m>/<m>.hpp`, with its archive under the baked lib dir | No — this is how the first-party C++ stdlib (`io`, `math`, …) resolves |

A few consequences worth internalizing:

- **Local wins.** A module sitting next to your source, or under a `--import-root`,
  is found *before* anything on the environment path or in the baked stdlib. The
  source's own directory is always searched first — drop two `.purr`/`.hpp` files
  side by side and `import` between them just works, no configuration (Python-style).
- **`--import-root` is repeatable.** Each flag adds one search root. A package
  manager passes one per dependency — this is exactly what
  `cheatah_add_program(… IMPORT_ROOTS …)` does under biome, so `import pkg.mod` finds
  `<dep-path>/pkg/mod.hpp`.
- In branches 1 and 3, a co-located **`libcheatah_<m>.a`** is linked if it is present
  but is **not** required; a header-only (transparent) module needs no archive.
- If `m` resolves nowhere, purrc fails with a clear error naming the unresolved
  import and telling you to place it beside the source, pass `--import-root <dir>`, or
  declare it in `cheatah.toml`'s `[dependencies]` — instead of a confusing downstream
  C++ "no such file" error.

### Example per case

```purr
import io          # (3) first-party stdlib — baked toolchain root
import os.path     # (3) resolves the `os` stdlib module; `.path` is nested
import helpers     # (1) helpers.hpp sitting next to this main.purr
import mypkg.util  # (1) resolves `mypkg` from a --import-root <dir> (dep path)
```

```sh
# What a build (or biome) invokes for the last case:
purrc main.purr -o app.so --import-root ../mypkg-checkout
#   -> import mypkg.util  finds  ../mypkg-checkout/mypkg/mypkg.hpp
```

## Transparent vs opaque library modules

A cheatah library module is a `.purr` compiled with `purrc --emit-library` into an
importable header living in `namespace cheatah::<m>`. There are two flavors:

| Flavor | Flag | The `<m>.hpp` contains | Archive |
|---|---|---|---|
| **opaque** (default) | `purrc --emit-library <m>.purr` | the public API only; the implementation is hidden | compiled into a signed `libcheatah_<m>.a` |
| **transparent** | `purrc --emit-library --transparent <m>.purr` | the full generated C++ source, inlined | none — header-only |

`--transparent` is what the first-party standard library uses, so users (and the VS
Code extension's Go-to-Definition) always see the true C++. An external author who
runs plain `--emit-library` gets an opaque module that ships only the public surface
and hides the concrete implementation in the archive.

Either way, the module's `<m>.hpp` is **committed next to the `.purr`** together with
a `<m>.hpp.sha512` checksum sidecar — that sidecar is exactly the marker purrc uses in
branches 1 and 2 to recognize a cheatah library and to verify it. (Opaque builds also
emit `libcheatah_<m>.a` + `libcheatah_<m>.a.sha512`.)

## Verification — fail-closed on tamper {#verification}

Before compiling your program against any resolved **cheatah library module** (one
that has the `.hpp.sha512` sidecar), purrc verifies it. There are two levels:

1. **Checksum (always on).** purrc recomputes the SHA-512 of the header (and, for an
   opaque module, the archive) and compares it to the `.sha512` sidecar. Any change or
   corruption since the module was built is caught and the build stops. This is
   unconditional — you get integrity checking for free.

2. **Signature (opt-in, strict).** If the environment variable **`$CHEATAH_TRUST`**
   points to a trust list — a file of authorized signing public keys, one 64-hex
   Ed25519 key per non-comment line — then purrc *additionally* requires a valid
   Ed25519 signature. It reads a `.sig` sidecar next to the artifact, checks the
   signing key is in your trust list, and verifies the signature over the file's
   bytes. A missing signature, an untrusted key, or a bad signature **fails the
   build** (fail-closed). With `$CHEATAH_TRUST` unset (the default), verification is
   checksum-only — a tamper is still always detected, just not attributed to a key.

The signature sidecar is produced by building the module with `purrc --emit-library
--sign <keyfile>` (an Ed25519 secret-key file; generate a keypair with
`purrc --keygen <prefix>`). Plain local/external headers with no sidecar are not
verified here — their definitions are the surrounding build's responsibility.

## Environment variables & flags, at a glance

| Name | Kind | Effect |
|---|---|---|
| `--import-root <dir>` | flag (repeatable) | add `<dir>` to the front-of-line search roots (branch 1); a package manager passes one per dependency |
| `$CHEATAH_MODULE_PATH` | env (`:`-separated) | extra roots for **signed** cheatah library modules, searched ahead of the baked root (branch 2); biome sets it for fetched extensions |
| `$CHEATAH_TRUST` | env (path to a file) | trust list of authorized Ed25519 signing keys; when set, signatures are required (strict, fail-closed) |
| `--emit-library` / `--transparent` / `--sign <key>` | flags | build a module: opaque vs source-inlined, optionally Ed25519-signed |

## Where biome fits

[biome](biome.html) is the tool that automates passing these import roots for you.
From your `cheatah.toml`, it turns each `[dependencies]` entry into a
`--import-root`, puts each fetched extension on `$CHEATAH_MODULE_PATH`, and drives the
whole build through CMake — so `import` "just works" without you managing any of the
search order by hand. This page is what it is doing on your behalf.
</content>
