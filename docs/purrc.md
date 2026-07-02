# purrc — the compiler {#purrc}

<div class="cheetah-slogan">🐱 <em>Reads <code>.purr</code>, writes native code — via modern C++.</em> 🐆</div>

**`purrc`** is the cheatah compiler. It transpiles a `.purr` source file to modern C++, then
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

## Building programs vs. modules

| You want | Command |
|---|---|
| A runnable program | `purrc app.purr -o app.so` |
| An **importable library** (a `.purr` others `import`) | `purrc --emit-library [--transparent] app.purr -o app.hpp` |
| A fast **type-check** (for editors; no output) | `purrc --check app.purr` |

`--emit-library` writes a signed importable module — `--transparent` inlines the generated C++
into the header (what the first-party stdlib uses, so the true source is always visible);
without it the API ships in the header and the implementation in a compiled `libcheatah_<m>.a`.
`--reexport <ns>` also exposes the module under a second namespace; `--split` emits a portable
`.hpp` + `.cpp` pair. How a built module is then *found* by an `import` is the
[Imports & module resolution](imports.html) page.

## Useful flags

- **Resolution / linking:** `--import-root <dir>` (repeatable — one per dependency; the
  [package manager](biome.html) passes these for you), `--link <archive|-lflag>`,
  `--cxxflag <flag>` (forwarded to the C++ backend; also read from `CHEATAH_CXXFLAGS_EXTRA`).
- **Integrity sidecars:** `--checksum` (write `<out>.sha512`), `--sign <keyfile>`
  (Ed25519 `<out>.sig`), `--runtime` (write the `<out>.rt` build-runtime manifest),
  `--sign-runtime <keyfile>` (a *separate* runtime key), and `--keygen <prefix>` to generate a
  keypair. These feed the runtime's opt-in verification.
- **Codegen control:** `--validate-cpp` (validate the generated C++ before compiling),
  `--no-remove-variables` / `--no-optimize-cpp` (disable dead-local elimination / all C++
  optimizations), `--no-crypto-selftest`.

Run `purrc --help` for the exact, current flag list.

## See also

- [The cheatah runtime](runtime.html) — what loads and runs the `.so` purrc builds.
- [Imports & module resolution](imports.html) — how `import` finds modules purrc emits.
- [biome](biome.html) — the package manager that invokes purrc with the right roots.
