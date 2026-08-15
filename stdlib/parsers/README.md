# parsers

Fast, safe, from-scratch input parsers — a **C++-authored** stdlib module (like `socket` and
`hashlib`): the parsers use templates, concepts, and `std::variant`, which are beyond the current
`.purr` subset. (`requests` is the first stdlib module written in pure cheatah.)

- <b>`parsers.json`</b> — a fast, SIMD-accelerated JSON parser:
  - `import parsers.json.Parser as Parser` — the reusable DOM parser: pooled zero-copy views or a
    self-contained owning Document; iterative grammar (no stack overflow at any nesting depth);
    a compile-time `Validate` switch; SIMD whitespace/string scanning.
  - the typed reader `read<T>()` parses straight into schema'd structs — purrc synthesizes the
    schema for `.purr` structs, so `json.read(text, q)` works on any struct you define.
- <b>`parsers.url`</b> — `import parsers.url.Parser as Parser` — the `http(s)` URL parser
  (`scheme://host[:port][/path][?query]`).
- <b>`parsers.html`</b> — `import parsers.html` — HTML escaping (`parsers.html.escape` /
  `unescape`) plus a tolerant tokenizing parser (`parsers.html.parse` returns the parse
  events as data): the rough equivalent of Python's `html` module + `html.parser`.
- <b>`parsers.xml`</b> — `import parsers.xml` — a tolerant XML reader that parses into a **slab
  DOM** navigated by integer node id (no pointers): `parsers.xml.parse(text)` then
  `find`/`findall`/`iter` + `attr`/`text`. Iterative (no stack overflow at any depth) and
  lenient. Built to feed cheatah's own tooling (e.g. reading Doxygen XML).

The module runs clean under ASan + UBSan and Valgrind against adversarial corpora (every prefix
truncation, byte corruptions, escape/number edge cases, 100k-deep nesting).
