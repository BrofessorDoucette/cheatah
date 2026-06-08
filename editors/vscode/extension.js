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

/** @type {{byQualified: Map<string, any>, byModule: Map<string, any[]>, moduleHeader: Map<string,string>, modules: string[], perfMeta: any}} */
let db = { byQualified: new Map(), byModule: new Map(), moduleHeader: new Map(), modules: [], perfMeta: {} };

// If `word` (or `prefix.word`, for dotted modules like os.path) names a stdlib module,
// return that module key; else null. Lets hover/Ctrl-click treat a module name (in
// `import math` or the `math` of `math.sqrt`) as a link to the module's header.
function moduleKeyAt(word, prefix) {
  if (db.moduleHeader.has(word)) return word;
  const dotted = prefix ? prefix + "." + word : null;
  return dotted && db.moduleHeader.has(dotted) ? dotted : null;
}

// Set at activation — the extension's own folder, which bundles a copy of the stdlib
// headers (under `headers/`) so control-click works even outside the cheatah repo.
let EXT_ROOT = "";

function loadDb(context) {
  EXT_ROOT = context.extensionPath;
  const file = path.join(context.extensionPath, "data", "functions.json");
  const raw = JSON.parse(fs.readFileSync(file, "utf8"));
  const byQualified = new Map();
  const byModule = new Map();
  const moduleHeader = new Map(); // module name -> its C++ header (from any member's srcfile)
  for (const fn of raw.functions) {
    byQualified.set(fn.qualified, fn);
    const list = byModule.get(fn.module) || [];
    list.push(fn);
    byModule.set(fn.module, list);
    if (fn.module != null && fn.srcfile && !moduleHeader.has(fn.module)) {
      moduleHeader.set(fn.module, fn.srcfile);
    }
  }
  db = { byQualified, byModule, moduleHeader, modules: raw.modules || [], perfMeta: raw.perf_meta || {} };
}

function fmtNs(ns) {
  return ns < 100 ? `${ns.toFixed(2)} ns/call` : `${ns.toFixed(0)} ns/call`;
}

// One-line "Performance" summary from the merged @perf data + provenance versions.
function perfLine(p, meta) {
  if (!p) return null;
  if (p.kind === "numpy") return "numeric — see the NumPy comparison in the docs";
  if (p.kind === "note") return p.note;
  let s = `**${fmtNs(p.cheatah_ns)}** in cheatah`;
  const cmp = p.compare_ns != null ? p.compare_ns : p.python_ns;
  if (cmp != null && p.speedup != null) {
    const tgt = p.vs === "numpy" ? `NumPy ${meta.numpy || ""}`.trim() : `CPython ${meta.cpython || "3.x"}`;
    const faster = p.speedup >= 1;
    const factor = faster ? p.speedup : Math.round((10 / p.speedup)) / 10;
    s += ` · ${fmtNs(cmp)} in ${tgt} · **≈${factor}× ${faster ? "faster" : "slower"}**`;
  } else {
    s += " · *(no direct equivalent)*";
  }
  return s;
}

// Render a stdlib function entry as hover markdown — brief, params, returns, plus the
// at-a-glance facts a purr dev wants without leaving the editor: @perf, @complexity,
// @alloc, @test.
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
  // Facts block — @perf / @complexity / @alloc / @test, each on its own line and set
  // off from the prose above by a divider, so they read at a glance.
  const t = fn.tags || {};
  const facts = [];
  const pl = perfLine(fn.perf, db.perfMeta);
  if (pl) facts.push(`$(zap) **Performance** — ${pl}`);
  if (t.complexity) facts.push(`$(watch) **Complexity** — ${t.complexity}`);
  if (t.alloc) facts.push(`$(database) **Allocation** — ${t.alloc}`);
  if (t.test) {
    const extra = [t.crtest, t.systest].filter(Boolean).map((x) => `\`${x}\``).join(", ");
    facts.push(`$(beaker) **Tested by** — \`${t.test}\`${extra ? ", " + extra : ""}`);
  }
  if (facts.length) md.appendMarkdown("\n\n---\n\n" + facts.join("  \n") + "\n");
  if (fn.srcfile) md.appendMarkdown(`\n\n*Ctrl-click to open \`${fn.srcfile}\`*\n`);
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

// --- User-defined struct/interface parsing (from the open .purr file) --------
// A lightweight, brace-depth line scanner — enough to power hover docs for the
// types, fields, and methods a user declares in their own program. Returns:
//   { types: Map<name, def>, methods: Map<methodName, def[]> }
// where def = { name, kind, fulfills[], fields:[{name,type}], methods:[{name,sig,doc}],
//               doc, text } and method entries carry their owning type name.
function parseDefs(text) {
  const lines = text.split(/\r?\n/);
  const types = new Map();
  const methods = new Map();
  let pendingDoc = []; // contiguous leading `#` comment lines

  const docOf = () => pendingDoc.join("\n").trim();
  const countBraces = (s) => (s.match(/{/g) || []).length - (s.match(/}/g) || []).length;

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.trim();
    const cm = line.match(/^#\s?(.*)$/);
    if (cm) { pendingDoc.push(cm[1]); continue; }

    const head = line.match(/^(struct|interface)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(:\s*([A-Za-z0-9_,\s]+))?\s*{/);
    if (!head) { if (line) pendingDoc = []; continue; }

    const kind = head[1];
    const name = head[2];
    const fulfills = head[4] ? head[4].split(",").map((s) => s.trim()).filter(Boolean) : [];
    const def = { name, kind, fulfills, fields: [], methods: [], doc: docOf(), text: "", line: i };
    pendingDoc = [];

    // Walk the body by brace depth (depth 1 = struct body; methods open/close deeper).
    let depth = countBraces(raw);
    const bodyLines = [raw];
    let memberDoc = [];
    for (i = i + 1; i < lines.length && depth > 0; i++) {
      const ln = lines[i];
      bodyLines.push(ln);
      const t = ln.trim();
      const mc = t.match(/^#\s?(.*)$/);
      if (mc) { memberDoc.push(mc[1]); depth += countBraces(ln); continue; }
      if (depth === 1) {
        const fn = t.match(/^fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/);
        const fld = t.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_\[\], ]+)/);
        if (fn) {
          const m = { name: fn[1], sig: `fn ${fn[1]}(${fn[2]})`, doc: memberDoc.join("\n").trim(), type: name, kind, line: i };
          def.methods.push(m);
          const arr = methods.get(fn[1]) || [];
          arr.push(m);
          methods.set(fn[1], arr);
        } else if (fld) {
          def.fields.push({ name: fld[1], type: fld[2].trim() });
        }
      }
      memberDoc = t === "" ? memberDoc : [];
      depth += countBraces(ln);
    }
    i -= 1; // the for-loop will ++; we consumed up to the closing brace
    def.text = bodyLines.join("\n");
    types.set(name, def);
  }
  return { types, methods };
}

function renderType(def) {
  const md = new vscode.MarkdownString(undefined, true);
  const head = def.kind + " " + def.name + (def.fulfills.length ? " : " + def.fulfills.join(", ") : "");
  md.appendCodeblock(head + " { … }", "cheatah");
  if (def.doc) md.appendMarkdown("\n" + def.doc + "\n");
  if (def.fields.length) {
    md.appendMarkdown("\n**Fields**\n");
    for (const f of def.fields) md.appendMarkdown(`- \`${f.name}: ${f.type}\`\n`);
  }
  if (def.methods.length) {
    md.appendMarkdown("\n**Methods**\n");
    for (const m of def.methods) md.appendMarkdown(`- \`${m.sig}\`${m.doc ? " — " + m.doc.split("\n")[0] : ""}\n`);
  }
  if (def.fulfills.length) md.appendMarkdown(`\n*Implements* ${def.fulfills.join(", ")}\n`);
  return md;
}

function renderMember(m) {
  const md = new vscode.MarkdownString(undefined, true);
  if (m.field) {
    md.appendCodeblock(`${m.field.name}: ${m.field.type}`, "cheatah");
    md.appendMarkdown(`\nField of \`${m.owner}\`.\n`);
    return md;
  }
  md.appendCodeblock(m.sig, "cheatah");
  if (m.doc) md.appendMarkdown("\n" + m.doc + "\n");
  md.appendMarkdown(`\nMethod of \`${m.type}\`.\n`);
  return md;
}

function renderModule(modKey) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock("import " + modKey, "cheatah");
  const fns = db.byModule.get(modKey) || [];
  md.appendMarkdown(`\nThe \`${modKey}\` standard-library module${fns.length ? ` — ${fns.length} functions` : ""}.\n`);
  const hdr = db.moduleHeader.get(modKey);
  if (hdr) md.appendMarkdown(`\n*Ctrl-click to open \`${hdr}\`*\n`);
  return md;
}

const hoverProvider = {
  provideHover(document, position) {
    const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) return undefined;
    const word = document.getText(range);
    const prefix = prefixBefore(document, range);
    const defs = parseDefs(document.getText());

    // 1. Hovering a user struct/interface type name → show its definition.
    if (defs.types.has(word)) return new vscode.Hover(renderType(defs.types.get(word)), range);

    // 1b. Hovering a module name (e.g. `math` in `import math` / `math.sqrt`) → the module.
    const modKey = moduleKeyAt(word, prefix);
    if (modKey) return new vscode.Hover(renderModule(modKey), range);

    // 2. Hovering a member after `obj.` → a user method, or a struct field.
    if (prefix) {
      const ms = defs.methods.get(word);
      if (ms && ms.length) return new vscode.Hover(renderMember(ms[0]), range);
      for (const def of defs.types.values()) {
        const f = def.fields.find((x) => x.name === word);
        if (f) return new vscode.Hover(renderMember({ field: f, owner: def.name }), range);
      }
    }

    // 3. Fall back to the stdlib/builtins database (modules + UFCS builtins).
    const fn =
      (prefix && db.byQualified.get(prefix + "." + word)) || db.byQualified.get(word);
    if (fn) return new vscode.Hover(renderDoc(fn), range);
    return undefined;
  },
};

// Resolve a stdlib header path (e.g. "stdlib/math/math.hpp") to an absolute file that
// exists, so control-click can open it. Tries, in order: the `cheatah.root` setting
// (a purrc/runtime install), each open workspace folder (a dev in the cheatah repo),
// then the headers bundled inside the extension itself (so it works anywhere). The
// QA gate refreshes both the bundled headers and the installed extension.
function resolveHeader(srcfile) {
  const roots = [];
  const cfg = vscode.workspace.getConfiguration("cheatah").get("root");
  if (cfg) roots.push(cfg);
  for (const wf of vscode.workspace.workspaceFolders || []) roots.push(wf.uri.fsPath);
  if (EXT_ROOT) roots.push(path.join(EXT_ROOT, "headers"));
  for (const r of roots) {
    const abs = path.join(r, srcfile);
    if (fs.existsSync(abs)) return abs;
  }
  return null;
}

const definitionProvider = {
  provideDefinition(document, position) {
    const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) return undefined;
    const word = document.getText(range);
    const prefix = prefixBefore(document, range);
    const defs = parseDefs(document.getText());

    // A user struct/interface type → its declaration in this file.
    if (defs.types.has(word)) {
      return new vscode.Location(document.uri, new vscode.Position(defs.types.get(word).line, 0));
    }
    // A user method after `obj.` → its `fn` line in this file.
    if (prefix) {
      const ms = defs.methods.get(word);
      if (ms && ms.length) return new vscode.Location(document.uri, new vscode.Position(ms[0].line, 0));
    }
    // A module name (`import math`, or the `math` of `math.sqrt`) → its C++ header.
    const modKey = moduleKeyAt(word, prefix);
    if (modKey) {
      const abs = resolveHeader(db.moduleHeader.get(modKey));
      if (abs) return new vscode.Location(vscode.Uri.file(abs), new vscode.Position(0, 0));
    }
    // A stdlib function → its C++ header declaration (resolved against the setting /
    // workspace / bundled headers).
    const fn = (prefix && db.byQualified.get(prefix + "." + word)) || db.byQualified.get(word);
    if (fn && fn.srcfile && fn.srcline) {
      const abs = resolveHeader(fn.srcfile);
      if (abs) return new vscode.Location(vscode.Uri.file(abs), new vscode.Position(fn.srcline - 1, 0));
    }
    return undefined;
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
    vscode.languages.registerDefinitionProvider(LANG, definitionProvider),
    vscode.languages.registerCompletionItemProvider(LANG, completionProvider, ".")
  );
}

function deactivate() {}

module.exports = { activate, deactivate, parseDefs, loadDb, renderDoc, perfLine };
