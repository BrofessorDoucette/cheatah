# Standard library extensions {#extensions}

<div class="cheetah-slogan">🐱 <em>Opt-in packages that bolt onto the standard library — you build only what you use.</em> 🐆</div>

The standard library ships with the cheatah toolchain. **Extensions** are optional packages
that live in their own repositories and are pulled in on demand with the
[biome](biome.html) package manager (`biome add <name>`, then `import`). You build only what
you opt into — nothing here is compiled unless a project asks for it.

Three extensions are public today. **Read the "Depends on" column carefully:** two of them
rest on the standard library alone, but one — `cheatah-plot` — is built on *another
extension*, so adding it pulls in more than itself.

| Extension | `import` | Depends on |
|---|---|---|
| [`cheatah-gpu`](https://github.com/BrofessorDoucette/cheatah-gpu) | `import gpu.dispatch` | the **standard library only** (plus a system Vulkan/Metal userspace stack) |
| [`cheatah-space`](https://github.com/BrofessorDoucette/cheatah-space) | `import space.time` | the **standard library only** (`ndarray`) |
| [`cheatah-plot`](https://github.com/BrofessorDoucette/cheatah-plot) | `import plot` | **`cheatah-gpu`** — *another extension* — **and** the standard library + a GLFW windowing shim |

## cheatah-gpu — the GPU layer

The simplest way onto the GPU from cheatah: one shared interface over the native GPU APIs,
**Vulkan** and **Metal**, so you can dispatch compute without the usual bring-up pain.
`biome add cheatah-gpu`, then `import gpu.dispatch` (or the 1:1 `gpu.vulkan` / `gpu.metal`
surfaces).

**Depends on: the standard library only — no other extension.** Its only extra
requirements are *system* packages (the userspace Vulkan loader + validation layers on
Linux, or Metal on macOS, plus the Slang shader compiler), which biome's
`scripts/install-deps.sh` provisions. It does **not** build on `cheatah-plot` or
`cheatah-space` — the dependency arrow points the other way.

## cheatah-space — astronomy & spatial math

Vectorized astronomy and spatial-math routines. `space.time` (Julian Date, Modified Julian
Date, J2000, and the NASA CDF_EPOCH bridge) works today; `space.cdf` and `space.irbem` are
on the roadmap. `biome add cheatah-space`, then `import space.time`.

**Depends on: the standard library only — no other extension.** Everything is
concept-templated and vectorized over `ndarray` (SIMD); there is no GPU, plotting, or
cross-extension dependency.

## cheatah-plot — GPU plotting

Dead-simple cross-platform plotting: hand it some numbers, get a plot in a window, on
Vulkan or Metal without learning either. `biome add cheatah-plot`, then `import plot`.

**Depends on another extension — note this one.** Unlike the other two, cheatah-plot does
**not** stand on the standard library alone: it renders through **`cheatah-gpu`**'s easy
`gpu` layer (it requires `cheatah-gpu >= 0.4.0`), so `biome add cheatah-plot`
**transitively fetches `cheatah-gpu` too**. On top of that it needs the standard library
and a first-class **GLFW** windowing dependency (plotting owns a window). The full chain is:

```
cheatah-plot  →  cheatah-gpu  →  standard library
     └──────────────────────────────→ standard library (+ GLFW window)
```

## How the dependencies are declared

Each extension is an ordinary cheatah project with a `cheatah.toml` manifest. One that
depends on *another* extension lists it in that manifest's `[dependencies]` section, exactly
as your own project would — which is precisely why `biome add cheatah-plot` brings in
`cheatah-gpu` for you. See [biome](biome.html) for how the manifest drives the build, and
[Imports & module resolution](imports.html) for how each `import` is then resolved to a
module on disk.
