// A minimal stub of the `vscode` module API, just enough to unit-test extension.js's
// providers headlessly (node test/run.js). Only what the providers touch is modeled.
"use strict";

class Position {
  constructor(line, character) { this.line = line; this.character = character; }
}
class Range {
  constructor(a, b, c, d) {
    if (a instanceof Position) { this.start = a; this.end = b; }
    else { this.start = new Position(a, b); this.end = new Position(c, d); }
  }
}
class Location {
  constructor(uri, pos) { this.uri = uri; this.range = pos instanceof Range ? pos : new Range(pos, pos); this.pos = pos; }
}
class Hover {
  constructor(contents, range) { this.contents = contents; this.range = range; }
}
class MarkdownString {
  constructor() { this.value = ""; this.isTrusted = false; }
  appendMarkdown(s) { this.value += s; return this; }
  appendCodeblock(code, lang) { this.value += "\n```" + (lang || "") + "\n" + code + "\n```\n"; return this; }
}
class CompletionItem {
  constructor(label, kind) { this.label = label; this.kind = kind; }
}
class Diagnostic {
  constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; }
}
class SemanticTokensLegend {
  constructor(types, mods) { this.tokenTypes = types; this.tokenModifiers = mods; }
}
class SemanticTokensBuilder {
  constructor() { this.toks = []; }
  push(line, char, len, type, mods) { this.toks.push({ line, char, len, type, mods }); }
  build() { return { data: this.toks }; }
}
const Uri = { file: (p) => ({ fsPath: p, scheme: "file", toString: () => "file://" + p }) };

// Test hook: set the workspace folders the extension resolves module roots from.
const workspace = {
  _folders: [],
  _config: new Map(),
  get workspaceFolders() { return this._folders; },
  getConfiguration(_section) {
    const cfg = this._config;
    return { get: (key, dflt) => (cfg.has(key) ? cfg.get(key) : dflt) };
  },
  onDidOpenTextDocument: () => ({ dispose() {} }),
  onDidSaveTextDocument: () => ({ dispose() {} }),
  onDidChangeTextDocument: () => ({ dispose() {} }),
  onDidCloseTextDocument: () => ({ dispose() {} }),
  textDocuments: [],
};

const languages = {
  createDiagnosticCollection: () => ({ set() {}, delete() {}, dispose() {} }),
  registerHoverProvider: () => ({ dispose() {} }),
  registerDefinitionProvider: () => ({ dispose() {} }),
  registerCompletionItemProvider: () => ({ dispose() {} }),
  registerDocumentSemanticTokensProvider: () => ({ dispose() {} }),
};

module.exports = {
  Position, Range, Location, Hover, MarkdownString, CompletionItem, Diagnostic,
  SemanticTokensLegend, SemanticTokensBuilder, Uri, workspace, languages,
  CompletionItemKind: { Function: 2, Module: 8, Struct: 21, Enum: 12, EnumMember: 19 },
  DiagnosticSeverity: { Error: 0, Warning: 1 },
};
