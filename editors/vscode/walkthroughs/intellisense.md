# Hover docs & autocomplete

The extension ships IntelliSense for cheatah's standard library and built-ins,
generated from the same Doxygen comments as the docs site.

- **Hover** over a function — `math.sqrt`, `string.split`, `io.print`, `len`,
  `range`, … — to see its signature, parameters, and return value.
- **Autocomplete**: type a module name followed by `.` (e.g. `math.`) to get a
  list of that module's functions, each with its doc. In a bare context you'll
  see the importable module names plus the built-ins.
- **Your own definitions, too** — hover a `fn`, `struct`, `interface`, or `enum`
  declared in the same file to see the comment you wrote above it (its
  "docstring"). This takes priority over the stdlib, so a local `fn` never shows
  an unrelated same-named header. Enum members (`Color.RED`) hover and complete,
  and go-to-definition jumps to the declaration in your file.

The doc database lives in `data/functions.json`. Regenerate it after the stdlib
docs change with:

```bash
cd editors/vscode
npm run gen-docs        # = python3 scripts/gen-hover-docs.py
```

Hover docs cover the modules you `import` (`math`, `string`, `io`, `os`,
`os.path`, `random`, `time`, `datetime`, `hashlib`, `linalg`, `ndarray`,
`statistics`, `socket`) and the always-available built-ins.
