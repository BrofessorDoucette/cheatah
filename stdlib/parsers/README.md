# parsers

Safe, allocation-bounded input parsers — the first standard-library module written in **pure
cheatah** (`.purr`) instead of hand-written C++.

cheatah is templated C++ with concepts plus a transpiler, so this module is still just C++:
`purrc --emit-library --transparent` transpiles [`parsers.purr`](parsers.purr) into the
committed, generated header [`parsers.hpp`](parsers.hpp) under `namespace cheatah::parsers`.

- **Transparent.** The generated C++ source is inlined into `parsers.hpp`, so the true code
  is always visible and the VS Code extension resolves Go-to-Definition straight into it. (An
  external library author can instead emit an *opaque* module, shipping only the API in the
  header and hiding the implementation inside a signed `libcheatah_parsers.a`.)
- **Verified on import.** `parsers.hpp` carries a SHA-512 checksum sidecar
  (`parsers.hpp.sha512`); `import parsers` makes purrc verify it is unchanged before compiling
  a consumer against it — the same integrity guarantee module signing gives binary modules.
  Set `CHEATAH_TRUST` to additionally require a valid Ed25519 signature.

```
import parsers
```

The module is intentionally **empty** for now — only the mechanism is in place. Parsers will be
added as typed, bounds-checked functions (no unbounded allocation, no undefined behaviour on
malformed input). `parsers.hpp` is regenerated from `parsers.purr` at build time; the QA gate
fails if the committed header drifts from the source.
