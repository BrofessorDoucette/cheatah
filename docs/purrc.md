# purrc — the compiler {#purrc}

<div class="cheetah-slogan">🐱 <em>Reads <code>.purr</code>, writes native code — via modern C++.</em> 🐆</div>

<b>`purrc`</b> is the cheatah compiler. It transpiles a `.purr` source file to modern C++, then
invokes the system C++ compiler (`-O3 -march=native`) to produce a **native loadable module**
that the [cheatah runtime](runtime.html) loads and runs.

```sh
purrc hello.purr -o hello.so    # compile a program to a native module
cheatah hello.so                # run it
```

## The pipeline

```
 hello.purr → [ lexer → parser → codegen ] → hello.gen.cpp → system C++ compiler → hello.so
```

The front end (`compiler/`) lexes, parses, and lowers cheatah to value-semantic C++ — no raw
`new`/`delete`, structs to structs, interfaces to C++20 concepts, and so on (see
[cheatah ↔ C++](cpp.html)). The generated translation unit exports `purr_main`; there is no
`main()`. You can see exactly what it emits on the [Transpiler](transpiler.html) page.

## The three things purrc can produce

Which artifact you get is chosen by mode flags. Everything else on this page tunes one of these:

| You want | Command | Output |
|---|---|---|
| A runnable **program** | `purrc app.purr -o app.so` | a loadable module the runtime runs |
| An **importable library** (a `.purr` others `import`) | `purrc --emit-library [--transparent] app.purr -o app.hpp` | a checksummed `<m>.hpp`, Ed25519-signed with `--sign` (+ `libcheatah_<m>.a` when opaque) |
| A fast **type-check** (editors; no output) | `purrc --check app.purr` | nothing — just diagnostics |

How a built module is then *found* by an `import` is the
[Imports & module resolution](imports.html) page.

## Flags

purrc takes exactly one `.purr` input (the first non-flag argument). All flags:

### Mode — what to produce (mutually exclusive; default is "compile a program")

| Flag | Effect |
|---|---|
| `--emit-library` | Emit an importable cheatah **library module** (a signed header, `namespace cheatah::<m>`) instead of a runnable program. Computes its own default output name. |
| `--check` | **Type-check only**: syntax + type errors reported against the `.purr` (with source line numbers, for editors). Produces no module. |
| `--keygen <prefix>` | Standalone: generate an Ed25519 keypair (`<prefix>.key` / `<prefix>.pub`) and exit — builds nothing. |
| `--version`, `-v` | Print the purrc version and exit. |
| `--help`, `-h` | Print usage and exit. |

### Output & library shape

| Flag | Effect |
|---|---|
| `-o <path>` | Output path. Defaults to the input's stem plus the platform module extension (`hello.purr` → `hello.so`) for a program, or `<m>.hpp` for `--emit-library`. |
| `--transparent` | *(with `--emit-library`)* Inline the full generated C++ **into** the header — header-only, no archive. This is what the first-party stdlib uses so the true source is always visible. Without it, the header carries only the public API and the implementation is hidden in a compiled `libcheatah_<m>.a` (an *opaque* module). |
| `--split` | *(with `--emit-library`)* Transpile to a portable `<m>.hpp` + `<m>.cpp` pair and **stop** — no C++ compile, no archive. The host build compiles the `.cpp` with its own compiler/flags. |
| `--reexport <ns>` | *(with `--emit-library`)* Also expose the module under a second namespace `<ns>::<m>`, so a host writes `<ns>::<m>::…` — a namespace alias appended to the header, no hand-written shim. |
| `--base-header <f>` | Fold a hand-written C++ header into the module, inside `namespace cheatah::<m>`, ahead of the transpiled body, so the `.purr` calls it by bare name. The output becomes `<m>.gen.hpp`. Set automatically from a same-stem sibling `.hpp`. |
| `--base-source <f>` | Likewise fold a C++ source (program mode / opaque libraries). Set automatically from a same-stem sibling `.cpp`. |
| `--no-adjacent` | Do not auto-detect the same-stem sibling `.hpp`/`.cpp`; an explicit `--base-header`/`--base-source` still applies. |

### Resolution & linking

| Flag | Effect |
|---|---|
| `--import-root <dir>` | Add `<dir>` to the import search roots. **Repeatable** — one per dependency; the [package manager](biome.html) passes these for you. See [Imports](imports.html). |
| `--link <archive\|-lflag>` | Extra input for the **final link** (an archive path or a `-l` flag). **Repeatable** — supplies a dependency's compiled definitions the compiler itself doesn't build. |
| `--cxxflag <flag>` | Extra C++ **compile** flag, forwarded verbatim to the backend (e.g. `-fblocks`, `-DFOO`, `-I<dir>`). **Repeatable**; also read from `CHEATAH_CXXFLAGS_EXTRA`. |

### Integrity sidecars (feed the runtime's opt-in verification)

| Flag | Effect |
|---|---|
| `--checksum` | Write a `<out>.sha512` corruption-check sidecar. |
| `--sign <keyfile>` | Ed25519-<b>sign</b> the module with a code-signing key → `<out>.sig`. |
| `--runtime` | Write a `<out>.rt` build-runtime manifest (records the C runtime the module was built against). |
| `--sign-runtime <keyfile>` | Sign the `.rt` manifest with a **separate** runtime key → `<out>.rt.sig`. |

Generate keys with `--keygen`; the [runtime](runtime.html) page covers how these sidecars are checked.

### Codegen control

| Flag | Effect |
|---|---|
| `--validate-cpp` | Validate the generated C++ before compiling it *(hook is wired; currently a no-op)*. |
| `--no-remove-variables` | Keep unused locals — skip dead-local elimination. |
| `--no-optimize-cpp` | Disable **all** generated-C++ optimizations (today this is exactly the dead-local pass, i.e. same as `--no-remove-variables`; it's the umbrella for future opt-outs). |
| `--no-crypto-selftest` | Skip the hardware-crypto power-on self-test (trust CPUID for SIMD crypto). The self-test is **on by default**. |

### Environment variables

purrc reads three environment variables at run time (the rest of its configuration — the C++ compiler, flags, stdlib root — is baked in by CMake at build time):

| Variable | Effect |
|---|---|
| `CHEATAH_CXXFLAGS_EXTRA` | Whitespace-separated extra compile flags, appended like repeated `--cxxflag` (biome/CMake sets this for a whole build tree). |
| `CHEATAH_MODULE_PATH` | `:`-separated extra search roots for **signed** library modules (see [Imports](imports.html)). |
| `CHEATAH_TRUST` | Path to a trust list of authorized Ed25519 signing keys; when set, imported library modules must carry a valid signature (strict, fail-closed). |

## See also

- [The cheatah runtime](runtime.html) — what loads and runs the `.so` purrc builds.
- [Imports & module resolution](imports.html) — how `import` finds modules purrc emits.
- [biome](biome.html) — the package manager that invokes purrc with the right roots.
