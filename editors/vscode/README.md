# cheatah for VS Code

Syntax highlighting and editor configuration for the [cheatah](../../README.md)
language (`.purr`).

- Highlights cheatah's keywords (`let`, `fn`, `struct`, `if`/`else`, `while`,
  `for`/`in`, `try`/`except`/`raise`, `import`/`as`, `and`/`or`/`not`), strings,
  numbers, both comment styles (`#` and `//`), operators, types, and built-ins.
- **The `cpp { … }` escape hatch is highlighted as real C++** — the block body uses
  VS Code's built-in C/C++ grammar (`source.cpp`), with balanced-brace tracking.
- Bracket matching, auto-closing pairs, and comment toggling for `.purr`.

## Install

**Quick (symlink into your extensions folder):**

```bash
ln -s "$(pwd)/editors/vscode" ~/.vscode/extensions/cheatah-0.1.0
# then reload VS Code (Developer: Reload Window)
```

**Or package a `.vsix`** (needs [`vsce`](https://github.com/microsoft/vscode-vsce)):

```bash
cd editors/vscode
npx @vscode/vsce package          # produces cheatah-0.1.0.vsix
code --install-extension cheatah-0.1.0.vsix
```

Open any `.purr` file (e.g. [`stdlib/scripts/tour.purr`](../../stdlib/scripts/tour.purr))
to see it in action.

## Files

| File | Purpose |
|------|---------|
| `package.json` | extension manifest — registers the `cheatah` language + grammar |
| `language-configuration.json` | comments, brackets, auto-closing, indentation |
| `syntaxes/cheatah.tmLanguage.json` | the TextMate grammar (the highlighter) |

## Highlighting on GitHub

GitHub colorizes code via [github-linguist](https://github.com/github-linguist/linguist),
which recognizes a language only after it's merged there — and that has an
**adoption bar** (a grammar plus the extension being used across many public
repos). Until then:

- **`.purr` files on github.com** are mapped to Python highlighting by the repo's
  root [`.gitattributes`](../../.gitattributes) (`*.purr linguist-language=Python`).
- **README fenced code blocks** use ```` ```python ```` — the closest built-in
  highlighter GitHub offers.

`syntaxes/cheatah.tmLanguage.json` is the artifact a future Linguist submission
needs (Linguist consumes TextMate grammars), so the work here is reusable when
cheatah is ready to apply for native GitHub support.
