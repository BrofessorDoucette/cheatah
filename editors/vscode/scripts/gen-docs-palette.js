#!/usr/bin/env node
// gen-docs-palette.js — the VS Code extension theme is the SINGLE SOURCE OF TRUTH for cheatah's colors.
// This reads themes/cheatah-color-theme.json and emits the docs' CSS palette (the :root color
// variables doxygen-awesome + cheetah.css consume), so the docs DERIVE their colors from the editor
// theme. Edit the theme, re-run this, and the docs pick up the change — never the other way around.
//
//   node editors/vscode/scripts/gen-docs-palette.js
//
// Output: docs/theme/cheatah-palette.generated.css (loaded after cheetah.css, so it wins).
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..", "..", "..");                 // cheatah repo root
const THEME = path.join(ROOT, "editors/vscode/themes/cheatah-color-theme.json");
const OUT = path.join(ROOT, "docs/theme/cheatah-palette.generated.css");

const theme = JSON.parse(fs.readFileSync(THEME, "utf8"));
const sem = theme.semanticTokenColors || {};
const ui = theme.colors || {};

// First token color whose scope list contains `needle`.
function tok(needle) {
  for (const t of theme.tokenColors || []) {
    const scopes = Array.isArray(t.scope) ? t.scope : [t.scope];
    if (scopes.some((s) => s && s.includes(needle))) return t.settings && t.settings.foreground;
  }
  return null;
}

// Map the editor theme onto the doxygen/cheetah.css variable names. The semantic roles line up:
// keyword=clay, keywordtype=type(gold), token=string(olive), comment=warm gray, link=tan accent.
const vars = {
  "--primary-color": sem.enumMember,                 // tan accent
  "--primary-dark-color": ui["badge.background"],
  "--page-background-color": ui["editor.background"],
  "--page-foreground-color": ui["editor.foreground"],
  "--page-secondary-foreground-color": ui["statusBar.foreground"],
  "--separator-color": ui["editorWhitespace.foreground"],
  "--side-nav-background": ui["sideBar.background"],
  "--toc-background": ui["editor.lineHighlightBackground"],
  "--code-background": ui["input.background"],
  "--code-foreground": ui["editor.foreground"],
  "--fragment-background": ui["activityBar.background"],
  "--fragment-foreground": ui["editor.foreground"],
  "--fragment-keyword": tok("keyword"),              // "Keywords" entry → clay
  "--fragment-keywordtype": sem.type,                // type → gold-tan
  "--fragment-keywordflow": tok("keyword.control"),  // control flow → orange-clay
  "--fragment-token": tok("string"),                 // literals → olive/sage
  "--fragment-comment": tok("comment"),              // warm gray
  "--fragment-link": sem.enumMember,                 // tan, not blue
  "--fragment-preprocessor": tok("meta.preprocessor"),
};

let css =
  "/* GENERATED from editors/vscode/themes/cheatah-color-theme.json by\n" +
  " * editors/vscode/scripts/gen-docs-palette.js — DO NOT EDIT.\n" +
  " *\n" +
  " * The VS Code extension theme is the single source of truth for cheatah's colors; the docs derive\n" +
  " * from it. Edit the theme, re-run the generator, and these variables update. Loaded after\n" +
  " * cheetah.css so it overrides the (now fallback) hand-authored values there. */\n" +
  ":root {\n";
for (const [k, v] of Object.entries(vars)) {
  if (v) css += `    ${k}: ${v};\n`;
}
css += "}\n";

fs.writeFileSync(OUT, css);
const n = Object.values(vars).filter(Boolean).length;
console.log(`gen-docs-palette: wrote ${n} color variables -> ${path.relative(ROOT, OUT)}`);
