#!/usr/bin/env node
// Headless unit tests for the extension's hover/definition providers: run with
//   node editors/vscode/test/run.js
// The `vscode` module is replaced by test/stub/vscode.js; module headers resolve against
// test/fixtures (a generic `lab.sensor` user package pinning the token-module shape:
// descriptor struct + optional-returning opener + closer).
"use strict";

const path = require("path");
const Module = require("module");

// Route require("vscode") to the stub BEFORE loading the extension.
const stub = require("./stub/vscode.js");
const origLoad = Module._load;
Module._load = function (request, parent, isMain) {
  if (request === "vscode") return stub;
  return origLoad.call(this, request, parent, isMain);
};

const HERE = __dirname;
const EXT = path.dirname(HERE);
const FIXTURES = path.join(HERE, "fixtures");
stub.workspace._folders = [{ uri: { fsPath: FIXTURES } }];

const ext = require(path.join(EXT, "extension.js"));
ext.loadDb({ extensionPath: EXT });

// ---- a fake TextDocument over a .purr buffer -----------------------------------------
function makeDoc(text, fsPath) {
  const lines = text.split(/\r?\n/);
  return {
    languageId: "cheatah",
    uri: { fsPath, scheme: "file" },
    getText(range) {
      if (!range) return text;
      const lt = lines[range.start.line] || "";
      return lt.slice(range.start.character, range.end.character);
    },
    lineCount: lines.length,
    lineAt: (line) => ({ text: lines[line] || "" }),
    getWordRangeAtPosition(position, regex) {
      const lineText = lines[position.line] || "";
      const re = new RegExp(regex.source, "g");
      let m;
      while ((m = re.exec(lineText)) !== null) {
        if (m.index <= position.character && position.character <= m.index + m[0].length) {
          return new stub.Range(position.line, m.index, position.line, m.index + m[0].length);
        }
        if (m.index > position.character) break;
      }
      return undefined;
    },
  };
}

// Position of the Nth occurrence of `needle` in `text` (1-based), as a stub Position.
function at(text, needle, occurrence = 1) {
  const lines = text.split(/\r?\n/);
  let seen = 0;
  for (let i = 0; i < lines.length; i++) {
    let from = 0;
    for (;;) {
      const idx = lines[i].indexOf(needle, from);
      if (idx === -1) break;
      seen += 1;
      if (seen === occurrence) return new stub.Position(i, idx + 1);
      from = idx + 1;
    }
  }
  throw new Error(`fixture text is missing occurrence ${occurrence} of "${needle}"`);
}

// ---- the .purr program under test ----------------------------------------------------
const PURR = `import io
import lab.sensor as sensor

let count = 0
let dev = sensor.open_sensor(sensor.SensorDesc({
    .streaming = true,
    .tag_count = count,
}))
if dev.has_value() == false {
    io.print("no sensor")
} else {
    let d = dev.value()
    io.print(d.handle)
    sensor.close_sensor(d)
}
`;
const DOC = makeDoc(PURR, path.join(FIXTURES, "program.purr"));
const HDR = path.join(FIXTURES, "lab", "sensor", "sensor.hpp");
const hdrText = require("fs").readFileSync(HDR, "utf8");
const hdrLineOf = (needle) => hdrText.split(/\r?\n/).findIndex((l) => l.includes(needle));

// ---- tiny checker ---------------------------------------------------------------------
let failures = 0;
function check(name, fn) {
  try {
    fn();
    console.log(`  PASS ${name}`);
  } catch (e) {
    failures += 1;
    console.log(`  FAIL ${name}: ${e.message}`);
  }
}
function hoverText(pos) {
  const h = ext.hoverProvider.provideHover(DOC, pos);
  if (!h) return null;
  const c = h.contents;
  return (Array.isArray(c) ? c : [c]).map((x) => (x && x.value) || String(x)).join("\n");
}
function defAt(pos) {
  return ext.definitionProvider.provideDefinition(DOC, pos);
}
function expectHover(name, needlePos, mustContain) {
  check(`hover ${name}`, () => {
    const t = hoverText(needlePos);
    if (!t) throw new Error("no hover");
    for (const s of mustContain) {
      if (!t.includes(s)) throw new Error(`hover lacks "${s}" — got:\n${t.slice(0, 400)}`);
    }
  });
}
function expectDef(name, needlePos, file, line) {
  check(`definition ${name}`, () => {
    const d = defAt(needlePos);
    if (!d) throw new Error("no definition");
    const uri = d.uri || (d[0] && d[0].uri);
    const got = uri && uri.fsPath;
    if (got !== file) throw new Error(`lands in ${got}, want ${file}`);
    if (line != null) {
      const gotLine = (d.range && d.range.start.line) != null ? d.range.start.line : d.pos.line;
      if (gotLine !== line) throw new Error(`lands on line ${gotLine}, want ${line}`);
    }
  });
}

console.log("== extension provider tests (lab.sensor fixture) ==");

// 1. A module function call: `sensor.open_sensor(…)`.
expectHover("module fn open_sensor", at(PURR, "open_sensor"),
  ["open_sensor", "Open the first available sensor"]);
expectDef("module fn open_sensor", at(PURR, "open_sensor"),
  HDR, hdrLineOf("inline std::optional<Sensor> open_sensor"));

// 2. A struct constructed in a nested call: `sensor.SensorDesc({ … })`.
expectHover("module struct SensorDesc", at(PURR, "SensorDesc"),
  ["SensorDesc"]);
expectDef("module struct SensorDesc", at(PURR, "SensorDesc"),
  HDR, hdrLineOf("struct SensorDesc {"));

// 3. Optional methods on the opener's result: `dev.value()` / `dev.has_value()`.
expectHover("optional dev.value()", at(PURR, "value", 2),
  ["Sensor"]);
expectDef("optional dev.value()", at(PURR, "value", 2),
  HDR, hdrLineOf("struct Sensor {"));
expectHover("optional dev.has_value()", at(PURR, "has_value"),
  ["Sensor"]);

// 4. The closer, taking the unwrapped value: `sensor.close_sensor(d)`.
expectHover("module fn close_sensor", at(PURR, "close_sensor"),
  ["close_sensor", "Close an open sensor"]);
expectDef("module fn close_sensor", at(PURR, "close_sensor"),
  HDR, hdrLineOf("inline void close_sensor"));

// 5. A designated-init field inside the nested constructor: `.streaming = true`.
expectHover("designated-init field .streaming", at(PURR, "streaming"),
  ["streaming", "Stream continuously"]);
expectDef("designated-init field .streaming", at(PURR, "streaming"),
  HDR, hdrLineOf("bool streaming"));

// 6. A field of the unwrapped value: `d.handle` (d = dev.value()).
expectHover("unwrapped field d.handle", at(PURR, "handle"),
  ["handle"]);

// 7. MISS-THEN-CREATE: a header that appears AFTER the first (failed) lookup must resolve on the
// very next request — negative results are never cached (the stale-cache class of "no popup").
check("miss-then-create is not cached", () => {
  const os = require("os");
  const fs = require("fs");
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "cheatah-ext-test-"));
  try {
    stub.workspace._folders = [{ uri: { fsPath: tmp } }];
    if (ext.resolveModuleHeader("latemod") != null) throw new Error("resolved before creation");
    fs.mkdirSync(path.join(tmp, "latemod"));
    fs.writeFileSync(path.join(tmp, "latemod", "latemod.hpp"), "namespace cheatah { }\n");
    const got = ext.resolveModuleHeader("latemod");
    if (got !== path.join(tmp, "latemod", "latemod.hpp")) throw new Error(`still unresolved: ${got}`);
  } finally {
    stub.workspace._folders = [{ uri: { fsPath: FIXTURES } }];
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

// 8. ANCESTOR ROOTS: with the workspace pointing somewhere unrelated, a document INSIDE the
// package tree still resolves its module — the document's own ancestors are import roots,
// exactly like purrc treats the source tree.
check("document-ancestor roots resolve without a workspace", () => {
  const os = require("os");
  stub.workspace._folders = [{ uri: { fsPath: os.tmpdir() } }];
  ext._resetResolverCaches(); // a warm cache must not mask the root walk
  try {
    const t = hoverText(at(PURR, "open_sensor"));
    if (!t || !t.includes("Open the first available sensor")) {
      throw new Error(`no hover via ancestor roots — got: ${t && t.slice(0, 120)}`);
    }
  } finally {
    stub.workspace._folders = [{ uri: { fsPath: FIXTURES } }];
  }
});

// ---- from-import + interface: cheatah's `import Sym1, Sym2 from module` form --------------------
// The app imports RE-EXPORTED symbols from `pkg.textlex`. The tail (`textlex`) deliberately does NOT
// map to the physical file (pkg/text/lexer.purr) — resolution is by symbol UNDER the package, the
// re-exported-submodule scenario. Source-first: StrText/scan resolve to the .purr; a header-only helper (kind_name)
// and a concept (Drawable, a cheatah interface) fall through to the generated header.
const fs = require("fs");
function hoverTextOn(doc, pos) {
  const h = ext.hoverProvider.provideHover(doc, pos);
  if (!h) return null;
  const c = h.contents;
  return (Array.isArray(c) ? c : [c]).map((x) => (x && x.value) || String(x)).join("\n");
}
function expectDefOn(name, doc, needlePos, file, line) {
  check(`definition ${name}`, () => {
    const d = ext.definitionProvider.provideDefinition(doc, needlePos);
    if (!d) throw new Error("no definition");
    const uri = d.uri || (d[0] && d[0].uri);
    const got = uri && uri.fsPath;
    if (got !== file) throw new Error(`lands in ${got}, want ${file}`);
    if (line != null) {
      const gotLine = (d.range && d.range.start.line) != null ? d.range.start.line : d.pos.line;
      if (gotLine !== line) throw new Error(`lands on line ${gotLine}, want ${line}`);
    }
  });
}
function expectHoverOn(name, doc, needlePos, mustContain) {
  check(`hover ${name}`, () => {
    const t = hoverTextOn(doc, needlePos);
    if (!t) throw new Error("no hover");
    for (const s of mustContain) if (!t.includes(s)) throw new Error(`hover lacks "${s}" — got:\n${t.slice(0, 400)}`);
  });
}

console.log("== from-import + interface tests (pkg.textlex fixture) ==");

const APP = `import StrText, scan, kind_name from pkg.textlex
import Drawable from pkg.shapes

let x = scan(StrText({ .s = "hi" }))
`;
const APPDOC = makeDoc(APP, path.join(FIXTURES, "app.purr"));
const PKG_PURR = path.join(FIXTURES, "pkg", "text", "lexer.purr");
const PKG_GEN = path.join(FIXTURES, "pkg", "text", "lexer.gen.hpp");
const purrText = fs.readFileSync(PKG_PURR, "utf8");
const genText = fs.readFileSync(PKG_GEN, "utf8");
const purrLineOf = (needle) => purrText.split(/\r?\n/).findIndex((l) => l.includes(needle));
const genLineOf = (needle) => genText.split(/\r?\n/).findIndex((l) => l.includes(needle));

// A from-imported struct & function → their .purr definitions (source-first), on their usage sites.
expectDefOn("from-import struct StrText → .purr", APPDOC, at(APP, "StrText", 2),
  PKG_PURR, purrLineOf("struct StrText"));
expectDefOn("from-import fn scan → .purr", APPDOC, at(APP, "scan", 2),
  PKG_PURR, purrLineOf("fn scan"));
expectHoverOn("from-import hover scan", APPDOC, at(APP, "scan", 2),
  ["scan", "Scan a TextSource"]);
expectHoverOn("from-import hover StrText", APPDOC, at(APP, "StrText", 2), ["StrText"]);

// A header-only helper (no .purr definition) → the generated header (fallback), resolved by FIRST
// segment through the umbrella's #include — proving the tail need not map to a file.
expectDefOn("from-import header-only kind_name → .gen.hpp", APPDOC, at(APP, "kind_name", 1),
  PKG_GEN, genLineOf("kind_name"));

// A cheatah interface (a C++20 `concept`) imported from a different tail → the generated header.
expectDefOn("from-import concept Drawable → .gen.hpp", APPDOC, at(APP, "Drawable", 1),
  PKG_GEN, genLineOf("concept Drawable"));

// Interface navigation within a file: the base of `struct StrText: TextSource` and the `TextSource`
// parameter type both resolve to the `interface` declaration line.
const LEXDOC = makeDoc(purrText, PKG_PURR);
expectDefOn("interface base ref → interface decl", LEXDOC, at(purrText, "TextSource", 2),
  PKG_PURR, purrLineOf("interface TextSource"));
expectDefOn("interface param type → interface decl", LEXDOC, at(purrText, "TextSource", 3),
  PKG_PURR, purrLineOf("interface TextSource"));

// SECOND HOP: inside the .purr implementation, a symbol defined in the module's folded base header
// (same-stem .hpp/.gen.hpp) resolves to that header — an enum, a struct, and a helper function.
const HOP2 = `fn use() {
    let k = LexKind.WORD
    let s = StrText({ .s = "x" })
    return kind_name(k)
}`;
const HOP2DOC = makeDoc(HOP2, PKG_PURR);
expectDefOn("folded-base enum LexKind → .gen.hpp", HOP2DOC, at(HOP2, "LexKind", 1),
  PKG_GEN, genLineOf("enum class LexKind"));
expectDefOn("folded-base fn kind_name → .gen.hpp", HOP2DOC, at(HOP2, "kind_name", 1),
  PKG_GEN, genLineOf("kind_name"));

// Regression: a from-import must NOT poison the plain/aliased import parsing on other lines.
check("plain import unaffected by from-import", () => {
  const imp = ext.parseImports(APP);
  if (!imp.fromSym.has("StrText") || imp.fromSym.get("kind_name") !== "pkg.textlex")
    throw new Error("from-import symbols not collected");
  if (imp.locals.has("StrText")) throw new Error("from-import symbol wrongly treated as a module prefix");
});

if (failures) {
  console.log(`RESULT: FAIL (${failures} case(s))`);
  process.exit(1);
}
console.log("RESULT: PASS");
