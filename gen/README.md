# `gen/` — see what the transpiler emits

A scratch area for **reading the C++ that `purrc` generates**. Each `*.purr` here is a
small, complete program; next to it sits the `*.gen.cpp` that `purrc` produced for it.

Edit a `.purr`, regenerate, and diff the `.gen.cpp` to see how a language change moves
the output.

## Regenerate

Run the **VS Code task** `gen: regenerate .gen.cpp` (Terminal → Run Task…), or from a
shell:

```sh
bash gen/generate.sh
```

It builds the **latest** `purrc` (which also rebuilds the stdlib libraries it links),
compiles every `gen/*.purr`, and writes each program's generated C++ to
`gen/<name>.gen.cpp`. The compiled module artifacts land in `gen/.out/` (git-ignored).

Use a specific build preset with `CHEATAH_PRESET=debug bash gen/generate.sh`.

## The examples

| program | shows |
|---------|-------|
| `http_response.purr` | string **self-append** → one chained `((head += a) += b) += c;`, no temporaries |
| `fizzbuzz.purr` | ranges, `if`, floor division `//`, string building |
| `grades.purr` | a `struct` with a method, a `list`, method-call (UFCS) syntax |
| `palette.purr` | a scoped `enum` + `match`/`case` (the `enum class` lowering) |
| `vectors.purr` | `ndarray` arrays through `linalg` routines (the numeric core) |

Add your own: drop a `.purr` in here and re-run the task.
