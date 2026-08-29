# Standard library extensions {#extensions}

<div class="cheetah-slogan">🐱 <em>Opt-in packages that bolt onto the standard library — you build only what you use.</em> 🐆</div>

The standard library ships with the cheatah toolchain. **Extensions** are optional packages
that live in their own repositories and are pulled in on demand with the
[biome](biome.html) package manager (`biome add <name>`, then `import`). You build only what
you opt into — nothing here is compiled unless a project asks for it.

Versions are governed by the **[Biome Standard](biome.html#biome-standard)**: your project
pins one standard version, and biome resolves every extension to the exact release tag that
was tested with your toolchain. `biome add` only accepts extensions that are members of
your project's standard — an extension that has not yet passed the cross-member gate is
listed here but declined at `add` time, truthfully.

Four extensions are public today, and every one is a Biome Standard member. **Read the
"Depends on" column carefully:** two rest on the standard library alone, and two are built
on *other extensions*, so adding them pulls in more than themselves.

| Extension | `import` | Depends on |
|---|---|---|
| [`cheatah-gpu`](https://github.com/BrofessorDoucette/cheatah-gpu) | `import gpu.dispatch` | the **standard library only** (plus a system Vulkan/Metal userspace stack) |
| [`cheatah-gpu-linalg`](https://github.com/BrofessorDoucette/cheatah-gpu-linalg) | `import gpulinalg` | <b>`cheatah-gpu`</b> and the standard library (`linalg`/`ndarray`) |
| [`cheatah-space`](https://github.com/BrofessorDoucette/cheatah-space) | `import space.time` · `space.cdf` · `space.irbem` | the **standard library only** (`ndarray`, and `fixarray` for `space.irbem`) |
| [`cheatah-plot`](https://github.com/BrofessorDoucette/cheatah-plot) | `import plot` | <b>`cheatah-gpu`</b> + <b>`cheatah-gpu-linalg`</b> and the standard library — headless (no windowing dependency) |

## cheatah-gpu — the GPU layer

The simplest way onto the GPU from cheatah: one shared interface over the native GPU APIs,
**Vulkan** and **Metal**, so you can dispatch compute without the usual bring-up pain.
`biome add cheatah-gpu`, then `import gpu.dispatch` (or the 1:1 `gpu.vulkan` / `gpu.metal`
surfaces).

**Depends on: the standard library only — no other extension.** Its only extra
requirements are *system* packages (the userspace Vulkan loader + validation layers on
Linux, or Metal on macOS, plus the Slang shader compiler), which cheatah-gpu's
[`scripts/install-deps.sh`](https://github.com/BrofessorDoucette/cheatah-gpu/blob/main/scripts/install-deps.sh) provisions. It does **not** build on any other extension — the
dependency arrows point the other way.

## cheatah-gpu-linalg — GPU linear algebra

The device backend for the standard library's `linalg`: a `device_array` whose elements
live in GPU memory, with the SAME `linalg.matmul(...)` / `a + b` / `linalg.dot(...)` calls
dispatching to register-tiled GPU kernels by ordinary overload resolution — no new language
surface. `biome add cheatah-gpu-linalg`, then `import gpulinalg`; `gpulinalg.available()`
answers the "is there a GPU here?" question honestly, and everything degrades to the host
`linalg` when there is not.

<b>Depends on: `cheatah-gpu`</b> (its Metal/Vulkan surfaces and the software-emulated Metal
test device) and the standard library.

## cheatah-space — astronomy & spatial math

Astronomy and space physics. Three modules, all working: `space.time` (Julian Date, Modified
Julian Date, J2000, and the NASA CDF_EPOCH bridge); `space.cdf` (NASA Common Data Format I/O
written from scratch — no NASA library linked); and `space.irbem`, a from-scratch reimplementation
of the radiation-belt library [PRBEM/IRBEM](https://github.com/PRBEM/IRBEM) written to the
published papers — IGRF-14, external magnetospheric field models, the bounce and drift invariants,
L\* and drift shells, with the field-line and flux integrals evaluated in parallel on the GPU.
`biome add cheatah-space`, then `import space.time` (or `space.cdf`, `space.irbem`).

**Depends on: the standard library only — no other extension.** Everything is
concept-templated and vectorized over `ndarray` (SIMD); there is no GPU, plotting, or
cross-extension dependency.

## cheatah-plot — cross-platform plotting

Dead-simple plotting: hand it some numbers, get a PNG — rendered by cheatah-plot's OWN
compute rasterizer on Vulkan or emulated Metal when a device is present, and by a
bit-identical CPU reference everywhere else. Headless first (`plot.save` / `plot.render`);
windowing is a later `plot.window` layer. `biome add cheatah-plot`, then `import plot`.

**Depends on other extensions — note this one.** cheatah-plot renders through
<b>`cheatah-gpu`</b>'s raw, 1:1 Vulkan/Metal forwarders (cheatah-gpu has no separate "easy"
layer, by design — consumers own their orchestration), and its device-resident array math
rides <b>`cheatah-gpu-linalg`</b>'s `linalg` overloads. The full chain is:

```
cheatah-plot  →  cheatah-gpu-linalg  →  cheatah-gpu  →  standard library
     └────────────────┴───────────────────────┴─────────→ standard library
```

## How the dependencies stay coherent

Each extension is an ordinary cheatah project with a `cheatah.toml` manifest, and a manifest
*documents* what its extension builds on. But the thing that keeps an inter-extension
dependency **version-coherent** is the [Biome Standard](biome.html#biome-standard): when an
extension that depends on another joins a standard, the standard carries **both** members at
tags tested together, so there is never a "which cheatah-gpu does this cheatah-plot need?"
question — your standard answers it. cheatah-plot is a member of the current standard, at
the cheatah-gpu tags it was tested against. See [biome](biome.html) for how the
manifest drives the build, and [Imports & module resolution](imports.html) for how each
`import` is then resolved to a module on disk.
