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
const os = require("os");
const cp = require("child_process");

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

// ---- USER MODULES: imports, aliases, and generic header resolution ------------------
// The stdlib lives in the prebuilt DB above. A USER's own cheatah modules (any header that
// carries the `namespace cheatah { namespace <m> = …; }` import bridge) are resolved live from
// the workspace / configured roots — generically, by import name, with NO project-specific
// paths baked in — so hover, Ctrl-click, and green module coloring work for them too.

// Scan a buffer's `import` statements for the names usable as a `name.` module prefix and the
// dotted module path each maps to:
//   import a.b.c       -> local "a.b.c" (and head "a") -> path "a.b.c"
//   import a.b.c as x  -> local "x" -> path "a.b.c"
//   import a           -> local "a" -> path "a"
// `locals` is every name to treat as a module (green coloring); `pathOf` maps a local name to
// its dotted module path (for header resolution).
function parseImports(text) {
  const pathOf = new Map();
  const locals = new Set();
  const re = /^\s*(?:from\s+[A-Za-z0-9_.]+\s+)?import\s+([A-Za-z0-9_.]+)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/;
  for (const raw of text.split(/\r?\n/)) {
    const m = raw.match(re);
    if (!m) continue;
    const modPath = m[1];
    const alias = m[2];
    if (alias) {
      pathOf.set(alias, modPath);
      locals.add(alias);
    } else {
      pathOf.set(modPath, modPath);
      locals.add(modPath);
      locals.add(modPath.split(".")[0]); // the head is also a namespace prefix
    }
  }
  return { pathOf, locals };
}

// Infer the module a local variable's TYPE comes from, by spotting its construction from an imported
// module: `let m = geometry.Mesh(cfg)` → `m` resolves against module `geometry`'s header. A
// var's methods/fields live in the same header as the constructor it was built from, so this lets
// Ctrl-click / hover on `m.method` land on that method's declaration. Returns var ->
// { modPath, fn } for every `let v = <importLocal>.<fnOrCtor>(…)` in the file — `fn` is the origin
// callable, so the resolver can read its RETURN TYPE (e.g. `std::optional<T>`, the optional
// pattern every fallible module call uses). A second form chains through the unwrap:
// `let d = v.value()` inherits v's module with `unwrapped: true`, so d's fields/methods resolve
// against the optional's inner type.
function parseModuleVars(text, imp) {
  const vars = new Map();
  const reMod = /^\s*let\s+([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*\(/;
  for (const raw of text.split(/\r?\n/)) {
    const m = raw.match(reMod);
    if (!m) continue;
    if (imp.pathOf.has(m[2])) vars.set(m[1], { modPath: imp.pathOf.get(m[2]), fn: m[3] });
    else if (m[3] === "value" && vars.has(m[2])) vars.set(m[1], { ...vars.get(m[2]), unwrapped: true });
  }
  return vars;
}

// The inner T of a `std::optional<T>` in a declaration's return position — the type a fallible
// module call actually yields once unwrapped.
function optionalInner(signature) {
  const m = /std::optional<\s*([A-Za-z_][\w:]*)\s*>/.exec(signature || "");
  return m ? m[1] : null;
}

// Resolve a module-built var to the TYPE NAME its members belong to: the origin callable's
// declaration is found in the module header, and an optional return unwraps to its inner type.
// Returns { typeName, hdr } (hdr = the header declaring the origin) or null.
function moduleVarType(v) {
  const hdr = resolveModuleHeader(v.modPath);
  if (!hdr) return null;
  const origin = findHeaderEntityDeep(hdr, v.fn, { call: true });
  if (!origin) return null;
  const inner = optionalInner(origin.ent.signature);
  if (inner) return { typeName: inner, hdr: origin.hdr, optional: true };
  // A constructor call (`geometry.Mesh(…)`) — the type IS the callable's name.
  if (origin.ent.kind === "constructor" || origin.ent.kind === "type") return { typeName: v.fn, hdr: origin.hdr };
  return null;
}

// Find ONE FIELD of `struct/class typeName` in a header: its declaration line, type text, and the
// doc comment above it. Line-scans the struct body by brace depth (fields are depth-1 lines that
// declare a name without parentheses).
function parseStructField(headerText, typeName, fieldName) {
  const lines = headerText.split(/\r?\n/);
  const headRe = new RegExp("\\b(struct|class)\\s+" + typeName + "\\b");
  const fieldRe = new RegExp("^\\s*[A-Za-z_][\\w:<>,*&\\s]*?[\\s*&]" + fieldName + "\\s*(=|;|\\[)");
  for (let i = 0; i < lines.length; i++) {
    if (/^\s*(\/\/|\*|#)/.test(lines[i]) || !headRe.test(lines[i])) continue;
    let depth = 0, started = false;
    for (let j = i; j < lines.length; j++) {
      const t = lines[j];
      if (depth === 1 && !/^\s*(\/\/|\*|#)/.test(t) && !t.includes("(") && fieldRe.test(t)) {
        const decl = t.trim().replace(/\s*;\s*(\/\/.*)?$/, "");
        return { name: fieldName, decl, line: j, doc: docAbove(lines, j), owner: typeName };
      }
      depth += (t.match(/{/g) || []).length - (t.match(/}/g) || []).length;
      if (depth > 0) started = true;
      if (started && depth <= 0) break;
    }
    break; // the first matching struct head is the one
  }
  return null;
}

// The struct a DESIGNATED-INIT field belongs to: hovering `streaming` in
//   let s = mod.ThingDesc({
//       .streaming = true,
//   })
// has no `obj.` prefix — the `.` hangs on its own — so walk UPWARD (brace-balanced, bounded) for
// the nearest still-open `<importLocal>.<Type>({` constructor head. Returns
// { modPath, type } or null.
function designatedInitType(document, position, imp) {
  let balance = 0; // net braces between the hover line and the candidate head, walking upward
  for (let i = position.line; i >= 0 && position.line - i < 60; i--) {
    const text = document.lineAt(i).text;
    const upto = i === position.line ? text.slice(0, position.character) : text;
    const heads = [...upto.matchAll(/([A-Za-z_]\w*)\.([A-Za-z_]\w*)\s*\(\s*\{/g)];
    if (heads.length) {
      const h = heads[heads.length - 1];
      const after = upto.slice(h.index + h[0].length);
      const net = 1 + (after.match(/{/g) || []).length - (after.match(/}/g) || []).length + balance;
      if (net > 0 && imp.pathOf.has(h[1])) return { modPath: imp.pathOf.get(h[1]), type: h[2] };
    }
    balance += (upto.match(/{/g) || []).length - (upto.match(/}/g) || []).length;
    if (balance > 0) return null; // the enclosing block opened above without a constructor head
  }
  return null;
}

// Is the word at `range` a designated-init field — preceded directly by `.` that does NOT chain
// off an identifier (`{ .field = … }` rather than `obj.field`)?
function isDesignatedInit(document, range) {
  const line = document.lineAt(range.start.line).text;
  const i = range.start.character;
  return i > 0 && line[i - 1] === "." && (i === 1 || !/[A-Za-z0-9_)\]]/.test(line[i - 2]));
}

// The directories to resolve user module headers from: the `cheatah.root` install, any
// `cheatah.importRoots` the user configured (mirrors purrc's --import-root), each open
// workspace folder, then the headers bundled in the extension.
function moduleRoots() {
  const roots = [];
  const cfg = vscode.workspace.getConfiguration("cheatah");
  const wsFolders = (vscode.workspace.workspaceFolders || []).map((wf) => wf.uri.fsPath);
  const r = cfg.get("root");
  if (r) roots.push(r);
  const extra = cfg.get("importRoots");
  // A configured importRoot may be ABSOLUTE or RELATIVE. A relative one is resolved against each
  // workspace folder (so `"trading/bigbrain"` works portably for a project nested in the workspace,
  // mirroring how purrc takes --import-root paths). This is the knob for a monorepo whose cheatah
  // modules don't sit at the workspace root.
  if (Array.isArray(extra)) for (const e of extra) if (e) {
    if (path.isAbsolute(e)) roots.push(e);
    else if (wsFolders.length) for (const wf of wsFolders) roots.push(path.join(wf, e));
    else roots.push(e);
  }
  for (const wf of wsFolders) roots.push(wf);
  if (EXT_ROOT) roots.push(path.join(EXT_ROOT, "headers"));
  return roots;
}

const _headerCache = new Map(); // dotted module path -> absolute header path | null
const _hppIndex = new Map();    // root -> [absolute .hpp/.h paths] (lazy, per session)

// Index every header under a root once (skipping build/vendor dirs), cached for the session.
function indexHeaders(root) {
  if (_hppIndex.has(root)) return _hppIndex.get(root);
  const out = [];
  const SKIP = new Set([
    "node_modules", ".git", "build", "build-release", "build-headless",
    "dist", "out", ".cache", "cmake-build-debug", "cmake-build-release",
  ]);
  const walk = (dir, depth) => {
    if (depth > 9 || out.length > 40000) return;
    let ents;
    try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch (_e) { return; }
    for (const e of ents) {
      if (e.name.startsWith(".")) continue;
      const full = path.join(dir, e.name);
      if (e.isDirectory()) { if (!SKIP.has(e.name)) walk(full, depth + 1); }
      else if (e.isFile() && (e.name.endsWith(".hpp") || e.name.endsWith(".h"))) out.push(full);
    }
  };
  try { walk(root, 0); } catch (_e) { /* ignore */ }
  _hppIndex.set(root, out);
  return out;
}

// Resolve a dotted module path (e.g. "neural", "information.autoencoder") to its C++ header,
// generically: first the conventional layouts under each root, then a cached recursive scan
// scored by directory layout / the cheatah import bridge. Cached per module path.
function resolveModuleHeader(modPath) {
  if (_headerCache.has(modPath)) return _headerCache.get(modPath);
  const segs = modPath.split(".");
  const last = segs[segs.length - 1];
  const parent = segs.length > 1 ? segs[segs.length - 2] : null;
  const roots = moduleRoots();
  const rel = [
    segs.join("/") + ".hpp",
    segs.join("/") + "/" + last + ".hpp",
    last + ".hpp",
    last + "/" + last + ".hpp",
  ];
  for (const root of roots) {
    for (const r of rel) {
      const abs = path.join(root, r);
      if (fs.existsSync(abs)) { _headerCache.set(modPath, abs); return abs; }
    }
  }
  // Recursive fallback: a file named <last>.hpp, scored by how well it fits the module.
  const bridge = new RegExp("namespace\\s+cheatah\\s*\\{[\\s\\S]*?\\bnamespace\\s+" + last + "\\s*=");
  let best = null, bestScore = 0;
  for (const root of roots) {
    for (const abs of indexHeaders(root)) {
      if (path.basename(abs) !== last + ".hpp") continue;
      const dirBase = path.basename(path.dirname(abs));
      let score = 1; // name matches
      if (parent && dirBase === parent) score = 3;       // .../parent/last.hpp (a submodule)
      else if (!parent && dirBase === last) score = 3;   // .../last/last.hpp (an umbrella header)
      else {
        try { if (bridge.test(fs.readFileSync(abs, "utf8"))) score = 2; } catch (_e) { /* ignore */ }
      }
      if (score > bestScore) { bestScore = score; best = abs; }
    }
  }
  _headerCache.set(modPath, best);
  return best;
}

// Resolve a dotted module to its `.purr` SOURCE — for modules that are NOT compiled to a header but
// imported straight from source, Python-style: `import check` where `check.purr` sits next to the
// importing file (a test helper, a sibling utility). Searches the importing file's own directory
// FIRST (where purrc inserts the source dir as the first --import-root), then the configured roots.
// Cached per (modPath, docDir).
const _purrModCache = new Map();
function resolvePurrModule(modPath, docDir) {
  const key = docDir + "\0" + modPath;
  if (_purrModCache.has(key)) return _purrModCache.get(key);
  const segs = modPath.split(".");
  const last = segs[segs.length - 1];
  const rels = [
    segs.join("/") + ".purr",
    segs.join("/") + "/" + last + ".purr",
    last + ".purr",
    last + "/" + last + ".purr",
  ];
  let found = null;
  for (const root of [docDir, ...moduleRoots()]) {
    if (!root) continue;
    for (const r of rels) {
      const abs = path.join(root, r);
      if (fs.existsSync(abs)) { found = abs; break; }
    }
    if (found) break;
  }
  _purrModCache.set(key, found);
  return found;
}

// Parse an imported `.purr` source's declarations (types / methods / top-level fns), reusing the same
// parseDefs scanner the open-document path uses, so a sibling-source module hovers identically to a
// header-backed one. Cached per session (a closed sibling module rarely changes mid-edit).
const _purrDefsCache = new Map();
function parsePurrFile(abs) {
  if (_purrDefsCache.has(abs)) return _purrDefsCache.get(abs);
  let defs = { types: new Map(), methods: new Map(), functions: new Map() };
  try { defs = parseDefs(fs.readFileSync(abs, "utf8")); } catch (_e) { /* ignore */ }
  _purrDefsCache.set(abs, defs);
  return defs;
}

const _headerTextCache = new Map(); // abs path -> text
function readHeader(abs) {
  if (_headerTextCache.has(abs)) return _headerTextCache.get(abs);
  let txt = "";
  try { txt = fs.readFileSync(abs, "utf8"); } catch (_e) { /* ignore */ }
  _headerTextCache.set(abs, txt);
  return txt;
}

// Extract the Doxygen-style doc block (`/** … */` or `///` lines) immediately above a header
// declaration line, returned as plain prose+tags text (parsed later by parsePurrDoc, which the
// stdlib and .purr paths already share — the `@param/@complexity/@alloc/@test` convention is
// identical in the C++ headers).
function docAbove(lines, declIdx) {
  let k = declIdx - 1;
  while (k >= 0) {
    const t = lines[k].trim();
    if (t === "" || /^(\[\[|template\b|requires\b|inline\b|static\b|constexpr\b|friend\b|explicit\b|virtual\b|\[\[nodiscard\]\])/.test(t)) { k--; continue; }
    break;
  }
  if (k < 0) return "";
  if (/^\s*\/\/\//.test(lines[k])) {
    const docs = [];
    while (k >= 0 && /^\s*\/\/\//.test(lines[k])) { docs.unshift(lines[k].replace(/^\s*\/\/\/<?\s?/, "")); k--; }
    return docs.join("\n").trim();
  }
  if (!/\*\//.test(lines[k])) return "";
  const block = [];
  while (k >= 0) {
    block.unshift(lines[k]);
    if (/\/\*\*?/.test(lines[k])) break;
    k--;
  }
  return block.join("\n")
    .replace(/^\s*\/\*\*?/, "")
    .replace(/\*\/\s*$/, "")
    .split("\n")
    .map((l) => l.replace(/^\s*\*\s?/, ""))
    .join("\n")
    .trim();
}

// Find the declaration of `name` in a header. A name is often BOTH a TYPE and a CALLABLE of the same
// spelling — a `class State` and its constructor `State(...)`, or a struct plus a same-named factory —
// so collect the first of each and choose by USAGE: `opts.call` (the name is written `name(...)`)
// prefers the constructor/function; otherwise the type wins. Returns { name, signature, line, doc,
// kind: "type"|"constructor"|"function" } or null. Signature is gathered across continuation lines.
function parseHeaderEntity(headerText, name, opts = {}) {
  const lines = headerText.split(/\r?\n/);
  const esc = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const classRe = new RegExp("\\b(class|struct)\\s+" + esc + "\\b");
  const fnRe = new RegExp("\\b" + esc + "\\s*\\(");
  const gather = (i, kind) => {
    let sig = lines[i].trim();
    for (let j = i; j < lines.length && j < i + 6 && !/[;{]/.test(lines[j]); ) {
      j++;
      if (j < lines.length) sig += " " + lines[j].trim();
    }
    sig = sig.replace(/\s*[{;].*$/, "").replace(/\s+/g, " ").trim();
    return { name, signature: sig, line: i, doc: docAbove(lines, i), kind };
  };
  let typeMatch = null, fnMatch = null;
  for (let i = 0; i < lines.length && !(typeMatch && fnMatch); i++) {
    const t = lines[i];
    if (/^\s*(\/\/|\*|#)/.test(t)) continue; // skip comment lines
    if (!typeMatch && classRe.test(t)) typeMatch = gather(i, "type");
    if (!fnMatch && fnRe.test(t)) {
      const before = t.slice(0, t.search(fnRe)).replace(/\s+$/, "");
      // A CONSTRUCTOR is the class's name with NO return type: `name(...)`, optionally preceded by a
      // declaration specifier (explicit/constexpr/inline/...). A free function/factory is preceded by
      // a return-type token. Everything else with `name(` is a call/use, not a declaration — reject a
      // qualified/member use (`mod::name(` / `obj.name(`), an assignment, or a keyword-prefixed call.
      const ctorLike = before === "" || /\b(explicit|constexpr|inline|virtual|static|friend)$/.test(before);
      const fnLike = !/[.:]$/.test(before) && !before.includes("=") &&
        !/\b(return|case|new|delete|sizeof|and|or|not|in)$/.test(before) && /[\w>*&\]]$/.test(before);
      if (ctorLike || fnLike) fnMatch = gather(i, ctorLike ? "constructor" : "function");
    }
  }
  // A constructor only makes sense alongside its type; if there's no type, a no-return-type match was
  // a stray call, so fall back to the type (none) → null rather than mislabel it.
  if (fnMatch && fnMatch.kind === "constructor" && !typeMatch) fnMatch.kind = "function";
  return opts.call ? (fnMatch || typeMatch) : (typeMatch || fnMatch);
}

// Resolve a `#include "rel"` the way the compiler does: relative to the including header first, then
// against the configured module roots (-I dirs), then — since headers include by `-I` path, e.g.
// `learning/rl/rl.hpp` does `#include "value.hpp"` resolved via `-I learning` to `learning/value.hpp`
// — the nearest indexed header of that basename (the one sharing the longest path with the includer).
function resolveInclude(rel, fromDir) {
  const here = path.resolve(fromDir, rel);
  if (fs.existsSync(here)) return here;
  for (const root of moduleRoots()) {
    const direct = path.join(root, rel);
    if (fs.existsSync(direct)) return direct;
  }
  const base = path.basename(rel);
  let best = null, bestShared = -1;
  for (const root of moduleRoots()) {
    for (const h of indexHeaders(root)) {
      if (path.basename(h) !== base) continue;
      let shared = 0;
      const a = h.split(path.sep), b = fromDir.split(path.sep);
      while (shared < a.length && shared < b.length && a[shared] === b[shared]) shared++;
      if (shared > bestShared) { bestShared = shared; best = h; }
    }
  }
  return best;
}

// Find `name` in `hdrAbs`, FOLLOWING the header's `#include "..."` directives when it isn't declared
// there directly. A module umbrella often re-exports from a sibling header — e.g. `learning/rl/rl.hpp`
// does `#include "value.hpp"` + `using value::value_at;`, so the real declaration (and its docs) live
// in value.hpp. Returns { ent, hdr } from the header that actually declares it, so the hover shows the
// right signature and "defined in" target. Depth- and cycle-bounded.
function findHeaderEntityDeep(hdrAbs, name, opts = {}, depth = 4, seen = new Set()) {
  if (!hdrAbs || depth < 0 || seen.has(hdrAbs)) return null;
  seen.add(hdrAbs);
  const txt = readHeader(hdrAbs);
  if (!txt) return null;
  const ent = parseHeaderEntity(txt, name, opts);
  if (ent) return { ent, hdr: hdrAbs };
  const dir = path.dirname(hdrAbs);
  const incRe = /^[ \t]*#[ \t]*include[ \t]*"([^"]+)"/gm;
  let m;
  while ((m = incRe.exec(txt))) {
    const inc = resolveInclude(m[1], dir);
    if (inc) {
      const r = findHeaderEntityDeep(inc, name, opts, depth - 1, seen);
      if (r) return r;
    }
  }
  return null;
}

// Is the hovered/clicked `range` immediately followed by `(` — i.e. a call/constructor usage rather
// than a bare type reference? (Lets the resolver pick the constructor over the class, and vice versa.)
function isCallUsage(document, range) {
  const rest = document.lineAt(range.end.line).text.slice(range.end.character);
  return /^\s*\(/.test(rest);
}

function fmtNs(ns) {
  return ns < 100 ? `${ns.toFixed(2)} ns/call` : `${ns.toFixed(0)} ns/call`;
}

// One-line "Performance" summary from the merged @perf data + provenance versions.
function perfLine(p, meta) {
  if (!p) return null;
  if (p.kind === "numpy") return "numeric — see the NumPy comparison in the docs";
  if (p.kind === "note") return p.note;
  // Numeric-core entries (ndarray/linalg) carry a hand-maintained NumPy comparison in
  // MICROSECONDS (cheatah_us/numpy_us) rather than the ns/speedup shape below. Render it
  // directly — and, crucially, never throw: a malformed/foreign perf shape must not take
  // down hover or autocomplete for the whole module (that hid every linalg/ndarray symbol).
  if (p.kind === "numpy_cmp" || p.cheatah_us != null) {
    const fmtUs = (us) => `${us.toFixed(2)} µs/call`;
    if (p.cheatah_us == null) return null;
    let s = `**${fmtUs(p.cheatah_us)}** in cheatah`;
    if (p.numpy_us != null) {
      const faster = p.cheatah_us <= p.numpy_us;
      const factor = faster ? p.numpy_us / p.cheatah_us : p.cheatah_us / p.numpy_us;
      const tgt = `NumPy ${meta.numpy || ""}`.trim();
      s += ` · ${fmtUs(p.numpy_us)} in ${tgt} · **≈${factor.toFixed(1)}× ${faster ? "faster" : "slower"}**`;
    }
    if (p.dims) s += ` · ${p.dims}`;
    return s;
  }
  if (p.cheatah_ns == null) return null;  // unknown perf shape — don't crash the provider
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

// Parse a .purr doc comment (the `#` block above a declaration) into the same shape a
// stdlib functions.json entry carries: { brief, detail, params, returns, tags }. The
// convention matches the stdlib headers — prose first, then @param/@return/@complexity/
// @alloc/@test/@crtest/@systest tags (a wrapped tag continues on the next line; a tag
// repeated, like several @systest entries, joins with ", "). With this, a function
// documented in an open .purr file hovers IDENTICALLY to one harvested from a generated
// or hand-written header — no header needs to exist for the local path.
function parsePurrDoc(doc) {
  const out = { brief: "", detail: "", params: [], returns: "", tags: {} };
  if (!doc) return out;
  const tagRe = /^@(param|returns?|complexity|alloc|test|crtest|systest)\b\s*(.*)$/;
  const prose = [];
  let cur = null; // the tag being collected: { kind, text: [..] }
  const flush = () => {
    if (!cur) return;
    const text = cur.text.join(" ").trim();
    if (cur.kind === "param") {
      const sp = text.indexOf(" ");
      if (sp === -1) out.params.push({ name: text, desc: "" });
      else out.params.push({ name: text.slice(0, sp), desc: text.slice(sp + 1).trim() });
    } else if (cur.kind === "return" || cur.kind === "returns") {
      out.returns = text;
    } else {
      out.tags[cur.kind] = out.tags[cur.kind] ? out.tags[cur.kind] + ", " + text : text;
    }
    cur = null;
  };
  for (const raw of doc.split("\n")) {
    const line = raw.trim();
    const m = line.match(tagRe);
    if (m) { flush(); cur = { kind: m[1], text: [m[2]] }; }
    else if (cur && line) cur.text.push(line); // a wrapped tag continuation
    else { flush(); prose.push(raw); }
  }
  flush();
  const blank = prose.findIndex((l) => l.trim() === "");
  if (blank === -1) out.brief = prose.join(" ").trim();
  else {
    out.brief = prose.slice(0, blank).join(" ").trim();
    out.detail = prose.slice(blank + 1).join("\n").trim();
  }
  return out;
}

// The shared hover body for a documented function — brief, detail, params, returns,
// plus the at-a-glance facts a purr dev wants without leaving the editor: @perf,
// @complexity, @alloc, @test/@crtest/@systest. Used by BOTH the stdlib-DB path and
// the local .purr scanner path so the popup is the same either way; the facts are
// joined with markdown hard breaks ("  \n") so EACH TAG RENDERS ON ITS OWN LINE.
function appendFnDoc(md, fn) {
  if (fn.brief) md.appendMarkdown("\n" + fn.brief + "\n");
  if (fn.detail) md.appendMarkdown("\n" + fn.detail + "\n");
  if (fn.params && fn.params.length) {
    md.appendMarkdown("\n**Parameters**\n");
    for (const p of fn.params) {
      md.appendMarkdown(`- \`${p.name}\`${p.desc ? " — " + p.desc : ""}\n`);
    }
  }
  if (fn.returns) md.appendMarkdown(`\n**Returns** — ${fn.returns}\n`);
  const t = fn.tags || {};
  const facts = [];
  const pl = perfLine(fn.perf, db.perfMeta);
  if (pl) facts.push(`$(zap) **Performance** — ${pl}`);
  if (t.complexity) facts.push(`$(watch) **Complexity** — ${t.complexity}`);
  if (t.alloc) facts.push(`$(database) **Allocation** — ${t.alloc}`);
  // A tag may hold several test names: comma-joined by the local .purr parser,
  // space-joined by Doxygen's text flattening of repeated tags. Split on either.
  const tested = [t.test, t.crtest, t.systest]
    .filter(Boolean)
    .flatMap((x) => x.split(/[,\s]+/).filter(Boolean))
    .map((s) => `\`${s}\``)
    .join(", ");
  if (tested) facts.push(`$(beaker) **Tested by** — ${tested}`);
  if (facts.length) md.appendMarkdown("\n\n---\n\n" + facts.join("  \n") + "\n");
}

// Render a stdlib function entry (functions.json, harvested from the Doxygen XML).
function renderDoc(fn) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(fn.signature, "cpp");
  appendFnDoc(md, fn);
  if (fn.srcfile) md.appendMarkdown(`\n\n*Ctrl-click to open*\n`);
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

// --- User-defined struct/interface/enum + function parsing (from the open .purr file)
// A lightweight, brace-depth line scanner — enough to power hover docs for the
// types, enums, fields, methods, and top-level functions a user declares in their own
// program. Returns:
//   { types: Map<name, def>, methods: Map<methodName, def[]>, functions: Map<name, fn> }
// where def = { name, kind, fulfills[], fields:[{name,type}], methods:[{name,sig,doc}],
//               members:[{name,value,doc}] (enums only), doc, text }, method entries
//   carry their owning type name, and fn = { name, sig, doc, line }.
function parseDefs(text) {
  const lines = text.split(/\r?\n/);
  const types = new Map();
  const methods = new Map();
  const functions = new Map(); // top-level `fn` definitions, with their leading comment
  let pendingDoc = []; // contiguous leading `#` comment lines

  const docOf = () => pendingDoc.join("\n").trim();
  const countBraces = (s) => (s.match(/{/g) || []).length - (s.match(/}/g) || []).length;

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.trim();
    const cm = line.match(/^#\s?(.*)$/);
    if (cm) { pendingDoc.push(cm[1]); continue; }

    // A top-level function: capture its signature and the contiguous `#` comment block
    // right above it (the "docstring" the equivalent Python would carry), so a hover
    // over a call to a function defined in THIS file shows that comment instead of
    // falling through to a same-named stdlib header.
    const fnHead = line.match(/^fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/);
    if (fnHead) {
      functions.set(fnHead[1], { name: fnHead[1], sig: `fn ${fnHead[1]}(${fnHead[2]})`, doc: docOf(), line: i });
      pendingDoc = [];
      continue;
    }

    // An enum: `enum Name { A [= v], … }` — members may share the head line
    // (comma/semicolon-separated) or sit on their own lines, each with an optional
    // leading `#` comment. Stored in `types` with kind "enum".
    const enumHead = line.match(/^enum\s+([A-Za-z_][A-Za-z0-9_]*)\s*{(.*)$/);
    if (enumHead) {
      const ename = enumHead[1];
      const edef = { name: ename, kind: "enum", fulfills: [], fields: [], methods: [], members: [], doc: docOf(), text: raw, line: i };
      pendingDoc = [];
      let memberDoc = [];
      const addMembers = (chunk) => {
        // Peel off a trailing inline `# comment` (its own doc for the members here),
        // then anything from a closing brace onward, before splitting into members.
        let inlineDoc = "";
        const hash = chunk.indexOf("#");
        if (hash !== -1) { inlineDoc = chunk.slice(hash + 1).trim(); chunk = chunk.slice(0, hash); }
        chunk = chunk.replace(/}.*$/, "");
        for (const tok of chunk.split(/[,;]/)) {
          const m = tok.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.+))?$/);
          if (m) edef.members.push({ name: m[1], value: m[2] ? m[2].trim() : null, doc: memberDoc.join("\n").trim() || inlineDoc });
        }
        memberDoc = [];
      };
      addMembers(enumHead[2]); // members sharing the head line after `{`
      let depth = countBraces(raw);
      const bodyLines = [raw];
      for (i = i + 1; i < lines.length && depth > 0; i++) {
        const ln = lines[i];
        bodyLines.push(ln);
        const t = ln.trim();
        const mc = t.match(/^#\s?(.*)$/);
        if (mc) { memberDoc.push(mc[1]); depth += countBraces(ln); continue; }
        addMembers(t);
        depth += countBraces(ln);
      }
      i -= 1; // the for-loop will ++; we consumed up to the closing brace
      edef.text = bodyLines.join("\n");
      types.set(ename, edef);
      continue;
    }

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
  return { types, methods, functions };
}

function renderType(def) {
  const md = new vscode.MarkdownString(undefined, true);
  const head = def.kind + " " + def.name + (def.fulfills.length ? " : " + def.fulfills.join(", ") : "");
  md.appendCodeblock(head + " { … }", "cheatah");
  const d = parsePurrDoc(def.doc); // struct/enum docs share the @tag convention
  appendFnDoc(md, d);
  if (def.kind === "enum") {
    if (def.members && def.members.length) {
      md.appendMarkdown("\n**Members**\n");
      for (const m of def.members) {
        const val = m.value != null ? " = " + m.value : "";
        const note = m.doc ? " — " + parsePurrDoc(m.doc).brief : "";
        md.appendMarkdown(`- \`${def.name}.${m.name}\`${val}${note}\n`);
      }
    }
    md.appendMarkdown("\n*A scoped enum (a C++ `enum class`).*\n");
    return md;
  }
  if (def.fields.length) {
    md.appendMarkdown("\n**Fields**\n");
    for (const f of def.fields) md.appendMarkdown(`- \`${f.name}: ${f.type}\`\n`);
  }
  if (def.methods.length) {
    md.appendMarkdown("\n**Methods**\n");
    for (const m of def.methods) {
      const brief = m.doc ? parsePurrDoc(m.doc).brief : "";
      md.appendMarkdown(`- \`${m.sig}\`${brief ? " — " + brief : ""}\n`);
    }
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
  appendFnDoc(md, parsePurrDoc(m.doc));
  md.appendMarkdown(`\nMethod of \`${m.type}\`.\n`);
  return md;
}

// A top-level user function: its signature plus the comment block above its `fn`
// definition (the local "docstring"), rendered through the same structured layout as a
// stdlib entry — so a local function hovers the same as an imported one.
function renderFunction(f, where) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(f.sig, "cheatah");
  appendFnDoc(md, parsePurrDoc(f.doc));
  md.appendMarkdown("\n*" + (where || "Defined in this file.") + "*\n");
  return md;
}

// A scoped-enum member: `Color.RED [= value]`, plus any comment above it.
function renderEnumMember(edef, mem) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(`${edef.name}.${mem.name}${mem.value != null ? " = " + mem.value : ""}`, "cheatah");
  if (mem.doc) md.appendMarkdown("\n" + mem.doc + "\n");
  md.appendMarkdown(`\nMember of enum \`${edef.name}\`.\n`);
  return md;
}

function renderModule(modKey) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock("import " + modKey, "cheatah");
  const fns = db.byModule.get(modKey) || [];
  md.appendMarkdown(`\nThe \`${modKey}\` standard-library module${fns.length ? ` — ${fns.length} functions` : ""}.\n`);
  const hdr = db.moduleHeader.get(modKey);
  if (hdr) md.appendMarkdown(`\n*Ctrl-click to open*\n`);
  return md;
}

// A function/class harvested live from a USER module's header — same structured layout as a
// stdlib entry, so an imported user function hovers identically to a stdlib one.
function renderHeaderEntity(ent, hdrAbs) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(ent.signature, "cpp");
  appendFnDoc(md, parsePurrDoc(ent.doc));
  md.appendMarkdown(`\n\n*Ctrl-click to open*\n`);
  return md;
}

// A FIELD harvested from a module header's struct — `desc.streaming`, `d.handle`, a designated
// initializer — with the doc comment above its declaration.
function renderHeaderField(f) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(f.decl, "cpp");
  appendFnDoc(md, parsePurrDoc(f.doc));
  md.appendMarkdown(`\nField of \`${f.owner}\`.\n`);
  md.appendMarkdown(`\n*Ctrl-click to open*\n`);
  return md;
}

// The optional-pattern accessors on a fallible module call's result: `v.has_value()` /
// `v.value()` where `let v = mod.open_thing(…)` returned `std::optional<T>`. Documented in terms
// of the ORIGIN call and the INNER type, which is what the reader actually wants to know.
function renderOptionalMethod(varName, word, inner, v) {
  const md = new vscode.MarkdownString(undefined, true);
  if (word === "has_value") {
    md.appendCodeblock(`${varName}.has_value() -> bool`, "cheatah");
    md.appendMarkdown(
      `\nWhether \`${v.fn}\` produced a \`${inner}\` — \`${v.fn}\` returns ` +
      `\`std::optional<${inner}>\` (the optional pattern: failure is an empty optional, ` +
      `never a sentinel or an exception).\n`);
  } else {
    md.appendCodeblock(`${varName}.value() -> ${inner}`, "cheatah");
    md.appendMarkdown(
      `\nThe \`${inner}\` inside the optional that \`${v.fn}\` returned. Only call after ` +
      `\`${varName}.has_value()\` — an empty optional throws.\n`);
  }
  md.appendMarkdown(`\n*Ctrl-click to open \`${inner}\`*\n`);
  return md;
}

// A user module name / import alias → a short "import" card linking to its header.
function renderUserModule(modPath, hdrAbs) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock("import " + modPath, "cheatah");
  md.appendMarkdown(`\nThe \`${modPath}\` module.\n`);
  if (hdrAbs) md.appendMarkdown(`\n*Ctrl-click to open*\n`);
  return md;
}

// ---- PACKAGE SYMBOL DBs: hover + semantic coloring for a module's exported API -------
// A cheatah package may ship a `symbols.json` next to a module path (e.g. cheatah-gpu's
// gpu/vulkan/symbols.json for `import gpu.vulkan`): { module, symbols: { Name: { kind,
// vk, doc } } }. Generic — any package can provide one — so the editor can color a
// member by its real kind (function vs type vs constant; a `vk.Instance()` call still
// reads as a type) and pop up its documentation link. Resolved from the same roots as
// module headers; cached per module path.
const _symbolsCache = new Map(); // dotted module path -> { Name: {kind, vk, doc} } | null
function loadModuleSymbols(modPath) {
  if (!modPath) return null;
  if (_symbolsCache.has(modPath)) return _symbolsCache.get(modPath);
  const rel = modPath.split(".").join(path.sep);
  let result = null;
  for (const root of moduleRoots()) {
    const cand = path.join(root, rel, "symbols.json");
    if (fs.existsSync(cand)) {
      try { result = JSON.parse(fs.readFileSync(cand, "utf8")).symbols || null; } catch (_e) { result = null; }
      if (result) break;
    }
  }
  _symbolsCache.set(modPath, result);
  return result;
}

// The semantic token index for a package symbol's kind (see semanticLegend): functions
// color as calls, every Vulkan type (struct/handle/enum/union/flags) as a type, and enum
// values / constants as enum members.
function symbolTokenType(kind) {
  if (kind === "function") return 3;            // function
  if (kind === "constant") return 2;            // enumMember
  return 1;                                     // type (struct, handle, enum, union, bitmask)
}

// The framework a package module belongs to, for hover labels (gpu.vulkan -> Vulkan, gpu.metal ->
// Metal, gpu.metal4 -> Metal 4). Generic: any package can ship a symbol DB; the module path names it.
function frameworkLabel(modPath) {
  const p = modPath || "";
  if (/metal4/.test(p)) return "Metal 4";
  if (/metal/.test(p)) return "Metal";
  if (/vulkan/.test(p)) return "Vulkan";
  const last = p.split(".").pop() || "module";
  return last.charAt(0).toUpperCase() + last.slice(1);
}

// Hover card for a package symbol: its qualified name, kind, the underlying native name, and a link to
// that object's documentation page. Module-aware so a Metal symbol reads "Metal class", not "Vulkan".
function renderModuleSymbol(localName, name, info, modPath) {
  const md = new vscode.MarkdownString(undefined, true);
  md.appendCodeblock(`${localName}.${name}`, "cheatah");
  const fw = frameworkLabel(modPath);
  const kinds = {
    function: "function", struct: "struct", handle: "handle", enum: "enum",
    union: "union", bitmask: "flags", constant: "constant", class: "class",
  };
  const kind = kinds[info.kind] || info.kind || "symbol";
  const native = info.vk || info.native;   // Vulkan DB uses `vk`, Metal DB uses `native`
  md.appendMarkdown(`\n${fw} ${kind}${native ? ` — \`${native}\`` : ""}\n`);
  if (info.doc) md.appendMarkdown(`\n[📖 ${fw} documentation ↗](${info.doc})\n`);
  return md;
}

const hoverProvider = {
  provideHover(document, position) {
    const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!range) return undefined;
    const word = document.getText(range);
    const prefix = prefixBefore(document, range);
    const defs = parseDefs(document.getText());
    // When the prefix names an imported MODULE (a stdlib module or a user import/alias), `prefix.word`
    // is a MODULE member — never a local method/field that merely shares the name. Resolve it through
    // the module path below; the local-member lookups are skipped so e.g. `dq.learn` (module) doesn't
    // get confused with a local `learn` function, and vice versa.
    const imp = parseImports(document.getText());
    const prefixIsModule =
      !!prefix && (imp.pathOf.has(prefix) || imp.locals.has(prefix) || db.moduleHeader.has(prefix));

    // 1. Hovering a user struct/interface type name → show its definition.
    if (defs.types.has(word)) return new vscode.Hover(renderType(defs.types.get(word)), range);

    // 1b. Hovering a module name (e.g. `math` in `import math` / `math.sqrt`) → the module.
    const modKey = moduleKeyAt(word, prefix);
    if (modKey) return new vscode.Hover(renderModule(modKey), range);

    // 1c. A member of a package that ships a symbol DB (e.g. `vk.CreateInstance`,
    // `vk.ApplicationInfo`, `vk.STRUCTURE_TYPE_*` for `import gpu.vulkan as vk`) → its kind
    // and a link to the official documentation.
    if (prefix && imp.pathOf.has(prefix)) {
      const mp = imp.pathOf.get(prefix);
      const syms = loadModuleSymbols(mp);
      if (syms && syms[word]) return new vscode.Hover(renderModuleSymbol(prefix, word, syms[word], mp), range);
    }

    // 2. Hovering a member after a LOCAL `obj.` (not a module) → an enum member, user method, or field.
    if (prefix && !prefixIsModule) {
      const et = defs.types.get(prefix);
      if (et && et.kind === "enum") {
        const mem = et.members.find((x) => x.name === word);
        if (mem) return new vscode.Hover(renderEnumMember(et, mem), range);
      }
      const ms = defs.methods.get(word);
      if (ms && ms.length) return new vscode.Hover(renderMember(ms[0]), range);
      for (const def of defs.types.values()) {
        const f = def.fields.find((x) => x.name === word);
        if (f) return new vscode.Hover(renderMember({ field: f, owner: def.name }), range);
      }
      // A member of a LOCAL var built from an imported module (`let m = geometry.Mesh(…)`;
      // hover `m.area`) → that method/field declaration in the module's header. A var whose
      // origin returned `std::optional<T>` also documents the optional accessors themselves
      // (`v.value()` / `v.has_value()`), and an UNWRAPPED var (`let d = v.value()`) resolves
      // its members as fields/methods of the inner T.
      const mv = parseModuleVars(document.getText(), imp);
      if (mv.has(prefix)) {
        const v = mv.get(prefix);
        const vt = moduleVarType(v);
        if (vt && vt.optional && !v.unwrapped && (word === "value" || word === "has_value")) {
          return new vscode.Hover(renderOptionalMethod(prefix, word, vt.typeName, v), range);
        }
        const hdr = resolveModuleHeader(v.modPath);
        const found = hdr && findHeaderEntityDeep(hdr, word, { call: isCallUsage(document, range) });
        if (found) return new vscode.Hover(renderHeaderEntity(found.ent, found.hdr), range);
        if (vt) {
          const f = parseStructField(readHeader(vt.hdr), vt.typeName, word);
          if (f) return new vscode.Hover(renderHeaderField(f), range);
        }
      }
    }

    // 2b. A designated-init field (`.streaming = true` inside `mod.ThingDesc({ … })`) — no
    // prefix hangs on the word, so recover the constructor head upward and resolve the field
    // in that struct's declaring header.
    if (!prefix && isDesignatedInit(document, range)) {
      const di = designatedInitType(document, position, imp);
      if (di) {
        const hdr = resolveModuleHeader(di.modPath);
        const owner = hdr && findHeaderEntityDeep(hdr, di.type, {});
        const f = owner && parseStructField(readHeader(owner.hdr), di.type, word);
        if (f) return new vscode.Hover(renderHeaderField(f), range);
      }
    }

    // 3. A top-level function defined in THIS file → its nearby comment. Checked before
    // the stdlib database so a local function never shows an unrelated same-named header.
    if (!prefix && defs.functions.has(word)) {
      return new vscode.Hover(renderFunction(defs.functions.get(word)), range);
    }

    // 4. Fall back to the stdlib/builtins database (modules + UFCS builtins).
    const fn =
      (prefix && db.byQualified.get(prefix + "." + word)) || db.byQualified.get(word);
    if (fn) return new vscode.Hover(renderDoc(fn), range);

    // 5. USER MODULES: a function/class from an imported header (resolved from the workspace),
    // or the module name / alias itself. Generic — no project paths baked in.
    const docDir = path.dirname(document.uri.fsPath);
    if (prefix && imp.pathOf.has(prefix)) {
      const modPath = imp.pathOf.get(prefix);
      const hdr = resolveModuleHeader(modPath);
      if (hdr) {
        const found = findHeaderEntityDeep(hdr, word, { call: isCallUsage(document, range) });
        if (found) return new vscode.Hover(renderHeaderEntity(found.ent, found.hdr), range);
      }
      // Fallback: a module imported straight from a sibling .purr SOURCE (no compiled header) —
      // e.g. `import check` where check.purr sits next to this file. Parse it and hover the fn/type.
      const purr = resolvePurrModule(modPath, docDir);
      if (purr) {
        const p = parsePurrFile(purr);
        const where = "Defined in `" + path.basename(purr) + "`.";
        if (p.functions.has(word)) return new vscode.Hover(renderFunction(p.functions.get(word), where), range);
        if (p.types.has(word)) return new vscode.Hover(renderType(p.types.get(word)), range);
      }
    }
    if (!prefix && imp.locals.has(word)) {
      const mp = imp.pathOf.get(word) || word;
      return new vscode.Hover(renderUserModule(mp, resolveModuleHeader(mp) || resolvePurrModule(mp, docDir)), range);
    }
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
    // A module prefix (`dq.learn`) resolves through the MODULE, never a local method/field of the
    // same name — so `dq.learn` lands on the module's `learn` while a bare `learn(…)` stays local.
    const imp = parseImports(document.getText());
    const prefixIsModule =
      !!prefix && (imp.pathOf.has(prefix) || imp.locals.has(prefix) || db.moduleHeader.has(prefix));

    // A user struct/interface type → its declaration in this file.
    if (defs.types.has(word)) {
      return new vscode.Location(document.uri, new vscode.Position(defs.types.get(word).line, 0));
    }
    // A user method after a LOCAL `obj.`, or an enum member after `Enum.` → its line in this file.
    if (prefix && !prefixIsModule) {
      const ms = defs.methods.get(word);
      if (ms && ms.length) return new vscode.Location(document.uri, new vscode.Position(ms[0].line, 0));
      const et = defs.types.get(prefix);
      if (et && et.kind === "enum" && et.members.some((x) => x.name === word)) {
        return new vscode.Location(document.uri, new vscode.Position(et.line, 0));
      }
      // A member of a LOCAL var built from an imported module (`let m = geometry.Mesh(…)`):
      // `m.method` → that method's declaration line in the module's header. The optional
      // accessors (`v.value()` / `v.has_value()`) jump to the INNER type's declaration — the
      // thing the reader is unwrapping — and an unwrapped var's members jump to the field line.
      const mv = parseModuleVars(document.getText(), imp);
      if (mv.has(prefix)) {
        const v = mv.get(prefix);
        const vt = moduleVarType(v);
        if (vt && vt.optional && !v.unwrapped && (word === "value" || word === "has_value")) {
          const t = findHeaderEntityDeep(vt.hdr, vt.typeName, {});
          if (t) return new vscode.Location(vscode.Uri.file(t.hdr), new vscode.Position(t.ent.line, 0));
        }
        const hdr = resolveModuleHeader(v.modPath);
        const found = hdr && findHeaderEntityDeep(hdr, word, { call: isCallUsage(document, range) });
        if (found) return new vscode.Location(vscode.Uri.file(found.hdr), new vscode.Position(found.ent.line, 0));
        if (vt) {
          const f = parseStructField(readHeader(vt.hdr), vt.typeName, word);
          if (f) return new vscode.Location(vscode.Uri.file(vt.hdr), new vscode.Position(f.line, 0));
        }
      }
    }
    // A designated-init field (`.streaming = …` inside `mod.ThingDesc({ … })`) → its declaration
    // line in the struct's header.
    if (!prefix && isDesignatedInit(document, range)) {
      const di = designatedInitType(document, position, imp);
      if (di) {
        const hdr = resolveModuleHeader(di.modPath);
        const owner = hdr && findHeaderEntityDeep(hdr, di.type, {});
        const f = owner && parseStructField(readHeader(owner.hdr), di.type, word);
        if (f) return new vscode.Location(vscode.Uri.file(owner.hdr), new vscode.Position(f.line, 0));
      }
    }
    // A top-level function defined in this file → its `fn` line (before any stdlib header).
    if (!prefix && defs.functions.has(word)) {
      return new vscode.Location(document.uri, new vscode.Position(defs.functions.get(word).line, 0));
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
    // A USER module (imported header): the module name/alias → its header; `mod.fn` → the
    // declaration line in that header. Resolved generically from the workspace/import roots.
    const docDir = path.dirname(document.uri.fsPath);
    if (!prefix && imp.locals.has(word)) {
      const hdr = resolveModuleHeader(imp.pathOf.get(word) || word);
      if (hdr) return new vscode.Location(vscode.Uri.file(hdr), new vscode.Position(0, 0));
      const purr = resolvePurrModule(imp.pathOf.get(word) || word, docDir);  // a sibling .purr module
      if (purr) return new vscode.Location(vscode.Uri.file(purr), new vscode.Position(0, 0));
    }
    if (prefix && imp.pathOf.has(prefix)) {
      const modPath = imp.pathOf.get(prefix);
      const hdr = resolveModuleHeader(modPath);
      if (hdr) {
        // follow re-exports to the declaring header; pick constructor vs type by usage
        const found = findHeaderEntityDeep(hdr, word, { call: isCallUsage(document, range) });
        if (found) return new vscode.Location(vscode.Uri.file(found.hdr), new vscode.Position(found.ent.line, 0));
        return new vscode.Location(vscode.Uri.file(hdr), new vscode.Position(0, 0));
      }
      // Sibling .purr source module (`import check` next to this file): jump to the fn/type line.
      const purr = resolvePurrModule(modPath, docDir);
      if (purr) {
        const p = parsePurrFile(purr);
        const e = p.functions.get(word) || p.types.get(word);
        return new vscode.Location(vscode.Uri.file(purr), new vscode.Position(e ? e.line : 0, 0));
      }
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

    // After `Name.` — offer a local enum's members before falling back to modules.
    const dotted = linePrefix.match(/([A-Za-z_][A-Za-z0-9_.]*)\.$/);
    if (dotted) {
      const head = dotted[1];
      const defs = parseDefs(document.getText());
      const et = defs.types.get(head);
      if (et && et.kind === "enum") {
        return et.members.map((m) => {
          const it = new vscode.CompletionItem(m.name, vscode.CompletionItemKind.EnumMember);
          it.detail = `${et.name}.${m.name}${m.value != null ? " = " + m.value : ""}`;
          it.documentation = renderEnumMember(et, m);
          return it;
        });
      }
      // After `module.` (incl. dotted like `os.path.`) → that module's functions.
      const items = (db.byModule.get(head) || []).map(functionItem);
      // Surface sub-modules (e.g. `os.` should also offer `path`).
      for (const m of db.modules) {
        if (m.startsWith(head + ".") && m.slice(head.length + 1).indexOf(".") === -1) {
          items.push(
            new vscode.CompletionItem(
              m.slice(head.length + 1),
              vscode.CompletionItemKind.Module
            )
          );
        }
      }
      return items.length ? items : undefined;
    }

    // Bare context → top-level module names + builtins + this file's enums & functions.
    const items = [];
    for (const m of db.modules) {
      if (m.indexOf(".") === -1) {
        items.push(new vscode.CompletionItem(m, vscode.CompletionItemKind.Module));
      }
    }
    for (const fn of db.byModule.get("") || []) items.push(functionItem(fn));
    const defs = parseDefs(document.getText());
    for (const [name, def] of defs.types) {
      const kind = def.kind === "enum" ? vscode.CompletionItemKind.Enum : vscode.CompletionItemKind.Struct;
      items.push(new vscode.CompletionItem(name, kind));
    }
    for (const [name, f] of defs.functions) {
      const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
      it.detail = f.sig;
      it.documentation = renderFunction(f);
      items.push(it);
    }
    return items;
  },
};

// ---- Semantic tokens: color module names & import ALIASES green like the stdlib modules ----
// The TextMate grammar can only color a fixed list of stdlib module names; it can't know the
// aliases a file introduces (`import geometry.mesh as mesh`). This provider marks every
// import local / alias used as a `name.` prefix — and the alias at its `as` site — as a
// `namespace` token, which themes color exactly like the grammar's module names.
const semanticLegend = new vscode.SemanticTokensLegend(["namespace", "type", "enumMember", "function"], []);
const semanticProvider = {
  provideDocumentSemanticTokens(document) {
    const imp = parseImports(document.getText());
    const locals = new Set(imp.locals);
    for (const m of db.moduleHeader.keys()) locals.add(m.split(".")[0]); // stdlib heads too
    const toks = []; // collected, then sorted before emit (the builder needs tokens in order)
    const lines = document.getText().split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      const text = lines[i];
      if (/^\s*#/.test(text)) continue; // skip comment lines
      // a module local used as a `name.` prefix
      const re = /([A-Za-z_][A-Za-z0-9_]*)\s*\./g;
      let m;
      while ((m = re.exec(text)) !== null) {
        if (locals.has(m[1])) toks.push({ line: i, char: m.index, len: m[1].length, type: 0 });
      }
      // the alias name in `import … as NAME`
      const am = text.match(/\bimport\b.*\bas\s+([A-Za-z_][A-Za-z0-9_]*)/);
      if (am) {
        const idx = text.lastIndexOf(am[1]);
        if (idx >= 0) toks.push({ line: i, char: idx, len: am[1].length, type: 0 });
      }
      // within a chain rooted at a module local, color PascalCase segments as a type and a non-call
      // member right after a type as an enumMember (the Color/RED of palette.Color.RED).
      const chainRe = /([A-Za-z_]\w*)((?:\.[A-Za-z_]\w*)+)/g;
      let cm;
      while ((cm = chainRe.exec(text)) !== null) {
        if (!locals.has(cm[1])) continue;
        const segBase = cm.index + cm[1].length; // index of the chain's first '.'
        // A package symbol DB (e.g. gpu.vulkan) gives each member's exact kind, so functions,
        // types and enum values color distinctly — `vk.CreateInstance` as a call, `vk.Instance`
        // as a type even though `vk.Instance()` looks like a call. Without one, fall back to the
        // PascalCase-is-a-type heuristic.
        const syms = loadModuleSymbols(imp.pathOf.get(cm[1]) || cm[1]);
        const segRe = /\.([A-Za-z_]\w*)/g;
        let sm, prevType = false;
        while ((sm = segRe.exec(cm[2])) !== null) {
          const name = sm[1];
          const at = segBase + sm.index + 1; // +1 skips the '.'
          if (syms && syms[name]) {
            const t = symbolTokenType(syms[name].kind);
            toks.push({ line: i, char: at, len: name.length, type: t });
            prevType = t === 1;
          } else if (/^[A-Z]/.test(name)) {
            toks.push({ line: i, char: at, len: name.length, type: 1 }); // type
            prevType = true;
          } else if (prevType && !/^\s*\(/.test(text.slice(at + name.length))) {
            toks.push({ line: i, char: at, len: name.length, type: 2 }); // enumMember (not a call)
            prevType = false;
          } else {
            prevType = false;
          }
        }
      }
    }
    toks.sort((a, b) => a.line - b.line || a.char - b.char);
    const b = new vscode.SemanticTokensBuilder(semanticLegend);
    for (const t of toks) b.push(t.line, t.char, t.len, t.type, 0);
    return b.build();
  },
};

// ---- Diagnostics: surface compiler errors in the editor -----------------------------
// cheatah is templated C++ with concepts + a transpiler, so the C++ backend is the source
// of truth for "is this valid?". `purrc --check` type-checks via the backend and, thanks
// to #line directives, reports every error against the .purr — a forgotten `let`, an
// unresolved symbol, a wrong argument count or type. We run it on the live buffer and
// publish the results as VS Code diagnostics.
let diagnostics; // DiagnosticCollection
const debounceTimers = new Map(); // uri -> timer

// Locate the purrc executable: the `cheatah.purrc` setting, else `<cheatah.root>/bin/purrc`,
// else `purrc` on PATH. Returns null only if a configured path doesn't exist.
function purrcPath() {
  const cfg = vscode.workspace.getConfiguration("cheatah");
  const explicit = cfg.get("purrc");
  if (explicit) return fs.existsSync(explicit) ? explicit : null;
  const root = cfg.get("root");
  if (root) {
    const p = path.join(root, "bin", "purrc");
    if (fs.existsSync(p)) return p;
  }
  return "purrc"; // resolved on PATH by execFile
}

// Parse purrc/clang stderr (`<file>:line:col: error|warning: message`) into diagnostics.
// Note/caret lines are ignored. A raw C++ "undeclared identifier" becomes a friendly
// cheatah hint to use `let`.
function parseDiagnostics(stderr, document) {
  const out = [];
  const re = /^(?:.*?):(\d+):(\d+):\s+(error|warning|fatal error):\s+(.*)$/;
  for (const raw of stderr.split("\n")) {
    const m = raw.match(re);
    if (!m) continue;
    const line = Math.max(0, parseInt(m[1], 10) - 1);
    const col = Math.max(0, parseInt(m[2], 10) - 1);
    const severity = m[3] === "warning"
      ? vscode.DiagnosticSeverity.Warning
      : vscode.DiagnosticSeverity.Error;
    let message = m[4];
    const undecl = message.match(/use of undeclared identifier '([A-Za-z_]\w*)'/);
    if (undecl) {
      message = `'${undecl[1]}' is not declared — introduce it with \`let ${undecl[1]} = …\` ` +
                `(cheatah needs \`let\` to declare a new variable).`;
    }
    const lineText = line < document.lineCount ? document.lineAt(line).text : "";
    const end = Math.max(col + 1, lineText.length);
    const range = new vscode.Range(line, col, line, end);
    const d = new vscode.Diagnostic(range, message, severity);
    d.source = "purrc";
    out.push(d);
  }
  return out;
}

function runDiagnostics(document) {
  if (!document || document.languageId !== LANG) return;
  if (!vscode.workspace.getConfiguration("cheatah").get("diagnostics.enable", true)) {
    diagnostics.delete(document.uri);
    return;
  }
  const purrc = purrcPath();
  if (!purrc) return; // configured path missing — stay quiet (best-effort)
  // Check the LIVE buffer (possibly unsaved) by writing it to a temp .purr. Diagnostics
  // carry line:col which we map back onto this document regardless of the temp path.
  const tmp = path.join(os.tmpdir(), `cheatah-check-${process.pid}-${document.version}.purr`);
  try {
    fs.writeFileSync(tmp, document.getText());
  } catch (_e) {
    return;
  }
  cp.execFile(purrc, ["--check", tmp], { timeout: 15000 }, (_err, _stdout, stderr) => {
    try { fs.unlinkSync(tmp); } catch (_e) { /* ignore */ }
    // ENOENT (purrc not found) lands here with empty stderr — just clear, no nagging.
    diagnostics.set(document.uri, parseDiagnostics(stderr || "", document));
  });
}

// Re-check ~400ms after the last keystroke, so we don't fork purrc on every character.
function scheduleDiagnostics(document) {
  const key = document.uri.toString();
  clearTimeout(debounceTimers.get(key));
  debounceTimers.set(key, setTimeout(() => runDiagnostics(document), 400));
}

function activate(context) {
  loadDb(context);
  diagnostics = vscode.languages.createDiagnosticCollection("cheatah");
  context.subscriptions.push(
    diagnostics,
    vscode.languages.registerHoverProvider(LANG, hoverProvider),
    vscode.languages.registerDefinitionProvider(LANG, definitionProvider),
    vscode.languages.registerCompletionItemProvider(LANG, completionProvider, "."),
    vscode.languages.registerDocumentSemanticTokensProvider(LANG, semanticProvider, semanticLegend),
    vscode.workspace.onDidOpenTextDocument(runDiagnostics),
    vscode.workspace.onDidSaveTextDocument(runDiagnostics),
    vscode.workspace.onDidChangeTextDocument((e) => scheduleDiagnostics(e.document)),
    vscode.workspace.onDidCloseTextDocument((doc) => diagnostics.delete(doc.uri))
  );
  // Check any .purr already open at activation.
  for (const doc of vscode.workspace.textDocuments) runDiagnostics(doc);
}

function deactivate() {}

module.exports = {
  activate, deactivate, parseDefs, loadDb, renderDoc, perfLine,
  parseImports, parseModuleVars, resolveModuleHeader, parseHeaderEntity, readHeader,
  definitionProvider, hoverProvider,
};
