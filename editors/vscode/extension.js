// cheatah VS Code extension — IntelliSense for the cheatah language (.purr).
//
// Adds hover documentation and autocomplete for the stdlib + builtins, driven by
// `data/functions.json` (generated from the Doxygen XML by
// `scripts/gen-hover-docs.py`). Highlighting, the icon theme, and the language
// configuration remain declarative in package.json — this file only powers the
// hover/completion providers.

const vscode = require("vscode");
const fs = require("fs");
const path = require("path");

const LANG = "cheatah";

/** @type {{byQualified: Map<string, any>, byModule: Map<string, any[]>, modules: string[]}} */
let db = { byQualified: new Map(), byModule: new Map(), modules: [] };

function loadDb(context) {
  const file = path.join(context.extensionPath, "data", "functions.json");
  const raw = JSON.parse(fs.readFileSync(file, "utf8"));
  const byQualified = new Map();
  const byModule = new Map();
  for (const fn of raw.functions) {
    byQualified.set(fn.qualified, fn);
    const list = byModule.get(fn.module) || [];
    list.push(fn);
    byModule.set(fn.module, list);
  }
  db = { byQualified, byModule, modules: raw.modules || [] };
}

// Render a function entry as hover markdown.
function renderDoc(fn) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(fn.signature, "cpp");
  if (fn.brief) md.appendMarkdown("\n" + fn.brief + "\n");
  if (fn.params && fn.params.length) {
    md.appendMarkdown("\n**Parameters**\n");
    for (const p of fn.params) {
      md.appendMarkdown(`- \`${p.name}\`${p.desc ? " — " + p.desc : ""}\n`);
    }
  }
  if (fn.returns) md.appendMarkdown(`\n**Returns** — ${fn.returns}\n`);
  return md;
}

// Capture a dotted module prefix sitting immediately before `range` (e.g. the
// `os.path` in `os.path.join`). Returns "" when the word stands alone.
function prefixBefore(document, range) {
  const line = document.lineAt(range.start.line).text;
  let i = range.start.character;
  if (i === 0 || line[i - 1] !== ".") return "";
  i -= 1; // skip the dot
  let end = i;
  while (i > 0 && /[A-Za-z0-9_.]/.test(line[i - 1])) i -= 1;
  return line.slice(i, end).replace(/\.+$/, "");
}

const hoverProvider = {
  provideHover(document, position) {
    const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) return undefined;
    const word = document.getText(range);
    const prefix = prefixBefore(document, range);
    const fn =
      (prefix && db.byQualified.get(prefix + "." + word)) || db.byQualified.get(word);
    if (!fn) return undefined;
    return new vscode.Hover(renderDoc(fn), range);
  },
};

function functionItem(fn) {
  const item = new vscode.CompletionItem(fn.name, vscode.CompletionItemKind.Function);
  item.detail = fn.signature;
  item.documentation = renderDoc(fn);
  return item;
}

const completionProvider = {
  provideCompletionItems(document, position) {
    const linePrefix = document.lineAt(position.line).text.slice(0, position.character);

    // After `module.` (incl. dotted like `os.path.`) → that module's functions.
    const dotted = linePrefix.match(/([A-Za-z_][A-Za-z0-9_.]*)\.$/);
    if (dotted) {
      const mod = dotted[1];
      const items = (db.byModule.get(mod) || []).map(functionItem);
      // Surface sub-modules (e.g. `os.` should also offer `path`).
      for (const m of db.modules) {
        if (m.startsWith(mod + ".") && m.slice(mod.length + 1).indexOf(".") === -1) {
          items.push(
            new vscode.CompletionItem(
              m.slice(mod.length + 1),
              vscode.CompletionItemKind.Module
            )
          );
        }
      }
      return items.length ? items : undefined;
    }

    // Bare context → top-level module names + builtins.
    const items = [];
    for (const m of db.modules) {
      if (m.indexOf(".") === -1) {
        items.push(new vscode.CompletionItem(m, vscode.CompletionItemKind.Module));
      }
    }
    for (const fn of db.byModule.get("") || []) items.push(functionItem(fn));
    return items;
  },
};

function activate(context) {
  loadDb(context);
  context.subscriptions.push(
    vscode.languages.registerHoverProvider(LANG, hoverProvider),
    vscode.languages.registerCompletionItemProvider(LANG, completionProvider, ".")
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
