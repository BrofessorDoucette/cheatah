# Changelog — cheatah VS Code extension

## 1.3.0

- Grammar: recognize the full importable stdlib module list — added `p256` and `x25519` (joining
  `memory`, `thread`, `regex`, and the rest) so `module.member` highlights everywhere.
- Highlight `constexpr` and `auto` as storage modifiers.
- Version aligned with the cheatah toolchain (1.3.0).
- Added a `LICENSE` file (MIT) and this changelog; refreshed the README; removed the committed
  `.vsix` build artifact from version control.

## 1.2.x

- Syntax highlighting, language configuration, IntelliSense (hover from the generated `functions.json`),
  and the file icon theme for `.purr`.
