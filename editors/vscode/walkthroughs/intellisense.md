# Hover docs & autocomplete

The extension ships IntelliSense for cheatah's standard library and built-ins,
generated from the same Doxygen comments as the docs site.

- **Hover** over a function — `math.sqrt`, `string.split`, `io.print`, `len`,
  `range`, … — to see its signature, parameters, and return value.
- **Autocomplete**: type a module name followed by `.` (e.g. `math.`) to get a
  list of that module's functions, each with its doc. In a bare context you'll
  see the importable module names plus the built-ins.

The doc database lives in `data/functions.json`. Regenerate it after the stdlib
docs change with:

```bash
cd editors/vscode
npm run gen-docs        # = python3 scripts/gen-hover-docs.py
```

Hover docs cover the modules you `import` (`math`, `string`, `io`, `os`,
`os.path`, `random`, `time`, `datetime`, `hashlib`, `linalg`, `ndarray`,
`statistics`, `socket`) and the always-available built-ins.
