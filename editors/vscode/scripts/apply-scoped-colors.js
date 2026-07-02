#!/usr/bin/env node
// apply-scoped-colors.js — GENERATE the cheatah color theme (a real, active VS Code theme) and select
// it. Unlike editor.tokenColorCustomizations (which VS Code never applies to hover/IntelliSense
// POPUPS), a THEME's tokenColors ARE used for popups — so this is the only way to color C++ `std::`
// etc. in popups. The theme is TWO-TIER:
//   1. GENERIC tier (plain scopes, Dark+-like)            -> every language looks ~normal.
//   2. CHEATAH tier (descendant `source.<lang> <scope>`)  -> overrides for cheatah / C / C++ / CMake
//      with a DIVERSE palette (not the old all-orange), and these descendant selectors work in the
//      editor AND in popups because they live in the theme.
// Output: themes/cheatah-color-theme.json (the contributed theme). Then settings select it and drop
// the now-redundant customizations. The extension must be reinstalled (theme file changed) + reloaded.
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");

// cheatah palette = authentic TOKYO NIGHT (designer-balanced; functions BLUE not yellow). Mapped onto
// cheatah's token classes from the canonical Tokyo Night (Night) hexes.
const P = {
  comment: "#a39a8c" /*lighter warm grey*/, docTag: "#c9c0b0" /*lighter still*/, keyword: "#aa8ae0" /*periwinkle violet*/, storage: "#9670cc" /*deep violet let*/,
  import: "#c98ad4" /*orchid purple*/, primitive: "#d0bef2" /*lighter lilac — C++ int/void/bool*/,
  modifier: "#a89a8e" /*warm grey*/, type: "#b89ee8" /*truer purple, less pink*/, module: "#73c990" /*green*/, call: "#9ece6a" /*lime green*/,
  funcName: "#9ece6a" /*lime green*/, string: "#b3ada3" /*light warm grey*/, number: "#ff9e64", boolean: "#ff9e64",
  constant: "#ff9e64", member: "#c2a878" /*tan/brown*/, preproc: "#8a8073" /*grey*/, macro: "#b59d7a" /*tan/brown*/, variable: "#e8e2d6" /*warm white*/,
  cast: "#d9799f" /*pink*/, operator: "#cdd2dc" /*near-white*/,
  genL1: "#9ece6a" /*green*/, genL2: "#d9799f" /*pink*/, genL3: "#ff9e64" /*orange*/, genL4: "#c4a7e0" /*lavender*/,
  genB1: "#6f9e4a", genB2: "#a85f7e", genB3: "#c47a3a", genB4: "#8a6fc0",
};

// Generic tier — load the REAL "Dark Modern" include chain (dark_modern -> dark_plus -> dark_vs) from
// the VS Code install and bake its FULL tokenColors / semanticTokenColors / chrome in, so every
// NON-cheatah language (Python, JSON, XML, JS, …) renders byte-identical to stock Dark Modern. (A
// hand-rolled approximation regressed them — this copies the genuine article.) The result is baked
// into the output theme JSON, so the shipped theme stays self-contained regardless of this path.
function loadDarkModern() {
  const bases = ["/usr/share/code", "/usr/lib/code", "/opt/visual-studio-code",
    "/usr/share/code-insiders", "/snap/code/current/usr/share/code"];
  let dir = null;
  for (const b of bases) {
    const d = path.join(b, "resources/app/extensions/theme-defaults/themes");
    if (fs.existsSync(path.join(d, "dark_modern.json"))) { dir = d; break; }
  }
  if (!dir) { console.warn("WARN: Dark Modern themes not found; other languages may regress."); return { tokenColors: [], semanticTokenColors: {}, colors: {} }; }
  const acc = { tokenColors: [], semanticTokenColors: {}, colors: {} };
  // walk the include chain root-first (dark_vs -> dark_plus -> dark_modern) so later files override.
  function visit(file) {
    const t = JSON.parse(fs.readFileSync(path.join(dir, file), "utf8"));
    if (t.include) visit(t.include.replace(/^\.\//, ""));
    if (Array.isArray(t.tokenColors)) acc.tokenColors.push(...t.tokenColors);
    Object.assign(acc.semanticTokenColors, t.semanticTokenColors || {});
    Object.assign(acc.colors, t.colors || {});
  }
  visit("dark_modern.json");
  return acc;
}
const DM = loadDarkModern();

// Editor chrome (dark, WARM brown/orange — user wants the chrome kept warm). Only the SYNTAX token
// colors are Tokyo Night; the background/sidebar/accents stay brown/orange. Brackets neutral-light;
// generic-<> rainbow is in tokenColors.
const colors = {
  "editor.background": "#0b0805", "editor.foreground": "#d4d4d4",
  "editorLineNumber.foreground": "#4a4234", "editorLineNumber.activeForeground": "#bdb29a",
  "editorCursor.foreground": "#cb9e52", "editor.selectionBackground": "#3a3024",
  "editor.lineHighlightBackground": "#0f0a06", "editorWhitespace.foreground": "#2e2820",
  "editorIndentGuide.background1": "#241f18", "editorIndentGuide.activeBackground1": "#4a4234",
  "editorBracketMatch.background": "#3a3024", "editorBracketMatch.border": "#cb9e52",
  "editorBracketHighlight.foreground1": "#d8d2c4", "editorBracketHighlight.foreground2": "#b0a89a",
  "editorBracketHighlight.foreground3": "#c2bcae", "editorBracketHighlight.foreground4": "#a89a8e",
  "editorBracketHighlight.foreground5": "#d8d2c4", "editorBracketHighlight.foreground6": "#b0a89a",
  "editorBracketHighlight.unexpectedBracket.foreground": "#cf5b3a",
  "editorGutter.background": "#0b0805", "editorError.foreground": "#cf5b3a", "editorWarning.foreground": "#d99a3c",
  "sideBar.background": "#080604", "sideBar.foreground": "#bdb29a", "sideBarTitle.foreground": "#8a8073",
  "sideBarSectionHeader.background": "#0f0a06", "activityBar.background": "#060402",
  "activityBar.foreground": "#cb9e52", "activityBar.inactiveForeground": "#4a4234",
  "activityBarBadge.background": "#cb9e52", "activityBarBadge.foreground": "#0b0805",
  "statusBar.background": "#0f0a06", "statusBar.foreground": "#bdb29a", "statusBar.noFolderBackground": "#0f0a06",
  "titleBar.activeBackground": "#080604", "titleBar.activeForeground": "#e3dccc", "titleBar.inactiveBackground": "#060402",
  "editorGroupHeader.tabsBackground": "#060402", "tab.activeBackground": "#0b0805", "tab.inactiveBackground": "#080604",
  "tab.activeForeground": "#e3dccc", "tab.inactiveForeground": "#7a7164", "tab.activeBorderTop": "#cb9e52",
  "panel.background": "#080604", "panelTitle.activeForeground": "#cb9e52",
  "terminal.background": "#0b0805", "terminal.foreground": "#d4d4d4",
  "input.background": "#1d1812", "input.border": "#2e2820", "dropdown.background": "#1d1812",
  "list.activeSelectionBackground": "#3a3024", "list.hoverBackground": "#1f1a14", "list.inactiveSelectionBackground": "#1f1a14",
  "focusBorder": "#9a5f2c", "badge.background": "#cb9e52", "badge.foreground": "#0b0805",
  "button.background": "#c2722e", "button.foreground": "#fff6ec", "progressBar.background": "#cb9e52",
  "scrollbarSlider.background": "#2e282099", "scrollbarSlider.hoverBackground": "#4a4234aa", "widget.border": "#2e2820",
};

const ROOTS = ["source.cheatah", "source.cpp", "source.c", "source.cmake"];
const DOXY = ["source.cpp", "source.c", "source.cheatah"];
function r(name, scope, fg, style) { const s = { foreground: fg }; if (style) s.fontStyle = style; return { name, scope, settings: s }; }
// descendant rule for the cheatah tier: color `bases` inside each root.
function lr(name, fg, bases, roots, style) {
  const scope = []; for (const ro of (roots || ROOTS)) for (const b of bases) scope.push(`${ro} ${b}`);
  return r(name, scope, fg, style);
}

const tokenColors = [
  // ---- GENERIC TIER: the genuine Dark Modern token list (all languages look stock) ----
  ...DM.tokenColors,

  // ---- CHEATAH TIER (cheatah/C/C++/CMake only — overrides the generic tier; reaches popups) ----
  lr("c-comment", P.comment, ["comment", "punctuation.definition.comment"], ROOTS, "italic"),
  lr("c-doc-body", P.comment, ["comment.block.documentation"], DOXY, "italic"),
  lr("c-doctag", P.docTag, ["storage.type.class.doxygen", "keyword.other.documentation"], DOXY),
  lr("c-string", P.string, ["string", "punctuation.definition.string"], ROOTS),
  lr("c-number", P.number, ["constant.numeric"], ROOTS),
  lr("c-keyword", P.keyword, ["keyword.control", "keyword.other", "keyword.operator.wordlike", "storage.type.function", "storage.type.struct", "storage.type.enum", "storage.type.class", "storage.type.union", "storage.type.template"], ROOTS),
  lr("c-import", P.import, ["keyword.control.import", "keyword.control.import.from", "keyword.other.import"], ROOTS),
  // C++ primitives (int/void/bool/uint32_t) — own lighter purple, distinct from user types.
  lr("c-primitive", P.primitive, ["storage.type.built-in"], ["source.cpp", "source.c"]),
  // template <class T>: the `class`/`typename` arg = deep violet; the param `T` = pink — three distinct.
  lr("c-template-arg", P.storage, ["storage.type.template.argument"], ["source.cpp", "source.c"]),
  lr("c-typeparam", P.cast, ["entity.name.type.template"], ["source.cpp", "source.c"]),
  lr("c-cast", P.cast, ["keyword.operator.cast", "keyword.operator.functionlike.cast"], ROOTS),
  lr("c-let", P.storage, ["storage.type.cheatah", "storage.modifier"], ["source.cheatah"]),
  lr("c-cpp-storage", P.type, ["storage.type"], ["source.cpp", "source.c"]),
  lr("c-cpp-modifier", P.modifier, ["storage.modifier"], ["source.cpp", "source.c"]),
  lr("c-type", P.type, ["support.type", "entity.name.type", "entity.name.class", "support.class"], ROOTS),
  lr("c-module", P.module, ["entity.name.namespace"], ROOTS),
  lr("c-call", P.call, ["entity.name.function", "entity.name.function.call", "support.function", "meta.function-call"], ROOTS),
  lr("c-funcname", P.funcName, ["entity.name.function.cheatah"], ["source.cheatah"], "bold"),
  lr("c-boolean", P.boolean, ["constant.language", "constant.language.boolean"], ROOTS),
  lr("c-constant", P.constant, ["constant.other.caps", "variable.other.constant", "constant.other", "entity.name.other.preprocessor.macro.predefined"], ROOTS),
  lr("c-member", P.member, ["variable.other.member", "variable.other.property"], ROOTS),
  lr("c-preproc", P.preproc, ["meta.preprocessor", "keyword.control.directive"], ROOTS),
  lr("c-macro", P.macro, ["entity.name.function.preprocessor", "meta.preprocessor.macro"], ["source.cpp", "source.c"]),
  lr("c-operator", P.operator, ["keyword.operator"], ROOTS),
  lr("c-variable", P.variable, ["variable", "meta.function.parameters"], ROOTS),
  // generic-<> rainbow (cheatah grammar) — diverse levels
  lr("gen-1", P.genL1, ["entity.name.type.generic.l1"], ["source.cheatah"]),
  lr("gen-2", P.genL2, ["entity.name.type.generic.l2"], ["source.cheatah"]),
  lr("gen-3", P.genL3, ["entity.name.type.generic.l3"], ["source.cheatah"]),
  lr("gen-4", P.genL4, ["entity.name.type.generic.l4"], ["source.cheatah"]),
  lr("gen-b1", P.genB1, ["punctuation.angle.l1"], ["source.cheatah"]),
  lr("gen-b2", P.genB2, ["punctuation.angle.l2"], ["source.cheatah"]),
  lr("gen-b3", P.genB3, ["punctuation.angle.l3"], ["source.cheatah"]),
  lr("gen-b4", P.genB4, ["punctuation.angle.l4"], ["source.cheatah"]),
];

// Semantic tokens — scoped per language so OTHER languages keep their normal semantic colors.
const sem = {
  namespace: P.module, type: P.type, class: P.type, struct: P.type, enum: P.type, interface: P.type,
  concept: P.type, typeParameter: "#d9799f", enumMember: "#ff9e64", function: P.call, method: P.call,
  macro: P.macro, property: P.member, variable: P.variable, "variable.readonly": P.constant,
  parameter: P.variable, "parameter.readonly": P.constant,
};
// Start from Dark Modern's generic semantic colors (so other languages' semantic tokens stay stock),
// then add the language-scoped cheatah overrides (these :lang keys never touch other languages).
const semanticTokenColors = { ...DM.semanticTokenColors };
for (const lang of ["cheatah", "cpp", "c", "cmake"]) for (const [k, v] of Object.entries(sem)) semanticTokenColors[`${k}:${lang}`] = v;

// Chrome: Dark Modern's full color set (every UI key defined) with the warm cheatah overrides on top.
const mergedColors = { ...DM.colors, ...colors };
const theme = { name: "cheatah", type: "dark", semanticHighlighting: true, colors: mergedColors, semanticTokenColors, tokenColors };
const THEME = path.resolve(__dirname, "..", "themes", "cheatah-color-theme.json");
fs.writeFileSync(THEME, JSON.stringify(theme, null, 2) + "\n");

// Settings: select the theme, drop the now-redundant customizations (the theme does it all + popups).
const SETTINGS = path.join(os.homedir(), ".config", "Code", "User", "settings.json");
let s = {};
try { s = JSON.parse(fs.readFileSync(SETTINGS, "utf8")); } catch (_e) { s = {}; }
s["workbench.colorTheme"] = "cheatah";
s["editor.semanticHighlighting.enabled"] = true;
s["workbench.tree.renderIndentGuides"] = "none";
delete s["editor.tokenColorCustomizations"];
delete s["editor.semanticTokenColorCustomizations"];
delete s["workbench.colorCustomizations"];
for (const l of ["cheatah", "cpp", "c", "cmake"]) delete s[`[${l}]`];
fs.writeFileSync(SETTINGS, JSON.stringify(s, null, 2) + "\n");

console.log(`wrote theme -> ${THEME} (${tokenColors.length} token rules)`);
console.log(`selected workbench.colorTheme = "cheatah"; removed editor customizations (theme handles popups too)`);
