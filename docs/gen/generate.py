#!/usr/bin/env python3
"""cheatah docs site generator.

Reads the Doxygen XML in docs/xml/ (Doxygen is used ONLY as the C++ parser) and
renders a fully custom, modern static site into docs/html/. No Doxygen HTML, no
doxygen-awesome — the layout, styling, navigation and search are all ours.

    python3 docs/gen/generate.py            # docs/xml -> docs/html

Dependency-free: standard library only (xml.etree + pathlib).
"""
from __future__ import annotations

import hashlib
import html
import json
import re
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

# Short content-hash query strings so browsers always fetch fresh CSS/JS on change.
VERS: dict[str, str] = {}

def _ver(text: str) -> str:
    return hashlib.md5(text.encode()).hexdigest()[:8]

ROOT = Path(__file__).resolve().parent.parent.parent        # repo root
XML = ROOT / "docs" / "xml"
OUT = ROOT / "docs" / "html"
ASSETS = Path(__file__).resolve().parent / "assets"

# Per-function benchmark numbers (regenerated periodically by scripts/perf_suite.py,
# NOT in the QA gate). The docs render a "Performance" row for any function found here,
# so the machine-specific numbers live in ONE provenance-tagged file, not 218 headers.
_PERF_FILE = ROOT / "docs" / "perf_data.json"
_perf = json.loads(_PERF_FILE.read_text()) if _PERF_FILE.exists() else {}
PERF = _perf.get("functions", {})
PERF_META = _perf.get("meta", {})
MOD_SHORT: dict[str, str] = {}   # namespace refid -> dotted module name (e.g. "os.path")
# A module's README.md (stdlib/<mod>/README.md) is merged in as the overview of that
# module's page instead of becoming a standalone page: {module short -> detail XML}.
MODULE_README: dict[str, object] = {}
# A module's classes are listed on the module's own page (not a separate sidebar column):
# {owning module short (e.g. "socket", "parsers::json") -> [class Compound, …]}.
MODULE_CLASSES: dict[str, list] = {}
_MOD_README_RE = re.compile(r"stdlib/(\w+)/README\.md$")

def _fmt_ns(ns: float) -> str:
    return f"{ns:.2f} ns/call" if ns < 100 else f"{ns:.0f} ns/call"

def _fmt_us(us: float) -> str:
    return f"{us:.2f} µs/op" if us < 100 else f"{us:.0f} µs/op"

def perf_row(refid: str, name: str) -> str:
    """Render the 'Performance' tag row for <module>.<name>, if perf_data covers it."""
    mod = MOD_SHORT.get(refid)
    e = PERF.get(f"{mod}.{name}") if mod else None
    if not e:
        return ""
    kind = e.get("kind")
    if kind == "numpy_cmp":
        # Per-function vs-NumPy measurement (µs/op at a representative size; the full
        # size-dependence is in the vs-NumPy table on this same module page).
        cu, nu = e["cheatah_us"], e["numpy_us"]
        speed = nu / cu if cu else 0.0
        faster = speed >= 1
        factor = speed if faster else (1 / speed if speed else 0.0)
        verb = "faster" if faster else "slower"
        cls = "perf-faster" if faster else "perf-slower"
        tgt = f'NumPy {PERF_META.get("numpy", "")}'.strip()
        dims = e.get("dims", "")
        body = (f'<strong>{_fmt_us(cu)}</strong> in cheatah · {_fmt_us(nu)} in {tgt} · '
                f'<strong class="{cls}">≈{round(factor, 1):g}× {verb}</strong>'
                + (f' <span class="perf-dims">({html.escape(dims)})</span>' if dims else ''))
    elif kind == "numpy":
        body = ('numeric — the honest baseline is NumPy/LAPACK, not a pure-Python loop; '
                'see the vs-NumPy table on this page')
    elif kind == "note":
        body = html.escape(e.get("note", ""))
    elif kind in ("compared", "cheatah_only"):
        body = f'<strong>{_fmt_ns(e["cheatah_ns"])}</strong> in cheatah'
        cmp = e.get("compare_ns", e.get("python_ns"))   # back-compat field name
        if kind == "compared" and cmp is not None:
            if e.get("vs") == "numpy":
                tgt = f'NumPy {PERF_META.get("numpy", "")}'.strip()
            else:
                tgt = f'CPython {PERF_META.get("cpython", "3.x")}'
            faster = e["speedup"] >= 1
            verb = "faster" if faster else "slower"
            factor = e["speedup"] if faster else round(1 / e["speedup"], 1)
            cls = "perf-faster" if faster else "perf-slower"
            body += (f' · {_fmt_ns(cmp)} in {tgt} · '
                     f'<strong class="{cls}">≈{factor:g}× {verb}</strong>')
        else:
            body += ' · <em>(no direct equivalent)</em>'
    else:
        return ""
    return (f'<div class="tag tag-perf"><span class="tag-k">Performance</span>'
            f'<span class="tag-v">{body}</span></div>')

# Compound kinds we turn into pages (in sidebar group order).
NS, CLASS, STRUCT, CONCEPT = "namespace", "class", "struct", "concept"

# Doxygen-internal index pages (the homepage + the @xrefitem buckets) — these are not
# standalone content; the homepage is rendered specially and the rest are skipped.
# Any OTHER `page` compound (e.g. a hand-written guide like Performance) is rendered.
XREF_PAGES = {"indexpage", "complexity", "alloc", "test", "crtest", "systest"}

# ---------------------------------------------------------------------------
# Code syntax highlighting — lightweight, theme-fitting, dependency-free.
#
# cheatah's keywords come straight from the compiler lexer (compiler/lexer.cpp,
# kKeywords); a few Python keywords are added so the side-by-side Python snippets in
# the guides also read nicely. Tokens are wrapped in <span class="tok-*"> and colored
# by the site CSS (.tok-kw/.tok-str/… in cheatah-docs.css), so highlighting matches
# the warm page theme instead of importing a generic highlighter.
# ---------------------------------------------------------------------------
_CHEATAH_KW = {"and", "as", "break", "case", "continue", "elif", "else", "enum", "except",
               "false", "fn", "for", "from", "if", "import", "in", "interface", "let",
               "match", "not", "or", "raise", "return", "struct", "true", "try", "while"}
# Extra keywords so the guides' Python snippets highlight too (superset, harmless).
_PYTHON_KW = {"def", "class", "lambda", "with", "yield", "pass", "global", "nonlocal",
              "del", "assert", "finally", "is", "None", "True", "False", "await", "async"}
_KEYWORDS = _CHEATAH_KW | (_PYTHON_KW - {"None", "True", "False"})
_CONSTANTS = {"true", "false", "None", "True", "False"}

_TOKEN_RE = re.compile(r"""
    (?P<com>\#[^\n]*)                                |  # line comment (#); // is floor-division
    (?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*') |  # single/double-quoted string
    (?P<num>\b\d+\.?\d*(?:[eE][+-]?\d+)?\b)          |  # number
    (?P<id>[A-Za-z_]\w*)                             |  # identifier / keyword
    (?P<other>.)                                       # any single other char
""", re.VERBOSE)


def highlight_code(text: str) -> str:
    """Tokenize a line of code and wrap tokens in themed <span>s (HTML-escaped)."""
    out: list[str] = []
    for m in _TOKEN_RE.finditer(text):
        kind, val = m.lastgroup, m.group()
        esc = html.escape(val)
        if kind == "com":
            out.append(f'<span class="tok-com">{esc}</span>')
        elif kind == "str":
            out.append(f'<span class="tok-str">{esc}</span>')
        elif kind == "num":
            out.append(f'<span class="tok-num">{esc}</span>')
        elif kind == "id":
            if val in _KEYWORDS:
                out.append(f'<span class="tok-kw">{esc}</span>')
            elif val in _CONSTANTS:
                out.append(f'<span class="tok-const">{esc}</span>')
            elif m.end() < len(text) and text[m.end()] == "(":  # call site
                out.append(f'<span class="tok-fn">{esc}</span>')
            else:
                out.append(esc)
        else:
            out.append(esc)
    return "".join(out)

# C++ highlighting for the generated `.gen.cpp` side of the transpiler page. The cheatah
# highlighter above can't be reused: in C++ `//` starts a comment (in .purr it's floor
# division) and `#` starts a preprocessor directive (in .purr it's a comment). Same themed
# token classes (tok-kw/str/num/com/fn), so both columns share the page's palette.
_CPP_KW = {"alignas", "alignof", "auto", "bool", "break", "case", "char", "class", "concept",
           "const", "constexpr", "continue", "decltype", "default", "delete", "do", "double",
           "else", "enum", "explicit", "extern", "float", "for", "friend", "if", "inline",
           "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator",
           "private", "protected", "public", "requires", "return", "short", "signed", "sizeof",
           "static", "static_assert", "struct", "switch", "template", "this", "throw", "true",
           "false", "try", "typedef", "typename", "union", "unsigned", "using", "virtual",
           "void", "volatile", "while"}
_CPP_CONST = {"true", "false", "nullptr"}

_CPP_TOKEN_RE = re.compile(r"""
    (?P<com>//[^\n]*)                                        |  # line comment
    (?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')         |  # string / char literal
    (?P<num>\b\d+\.?\d*(?:[eE][+-]?\d+)?(?:[uUlLfF]+)?\b)    |  # number (with int/float suffix)
    (?P<id>[A-Za-z_]\w*)                                     |  # identifier / keyword
    (?P<other>.)                                                # any single other char
""", re.VERBOSE)

# A preprocessor line: `#include <…>`, `#if defined(…)`, `#define NAME …`.
_CPP_PRE_RE = re.compile(r'(?P<dir>\#\s*[A-Za-z_]+)|(?P<inc><[^>\n]+>)|'
                         r'(?P<str>"(?:\\.|[^"\\\n])*")|(?P<other>.)')


def _highlight_cpp_line(text: str) -> str:
    if text.lstrip().startswith("#"):  # preprocessor directive
        out: list[str] = []
        for m in _CPP_PRE_RE.finditer(text):
            kind, esc = m.lastgroup, html.escape(m.group())
            if kind == "dir":
                out.append(f'<span class="tok-kw">{esc}</span>')
            elif kind in ("inc", "str"):
                out.append(f'<span class="tok-str">{esc}</span>')
            else:
                out.append(esc)
        return "".join(out)
    out = []
    for m in _CPP_TOKEN_RE.finditer(text):
        kind, val = m.lastgroup, m.group()
        esc = html.escape(val)
        if kind == "com":
            out.append(f'<span class="tok-com">{esc}</span>')
        elif kind == "str":
            out.append(f'<span class="tok-str">{esc}</span>')
        elif kind == "num":
            out.append(f'<span class="tok-num">{esc}</span>')
        elif kind == "id":
            if val in _CPP_KW:
                out.append(f'<span class="tok-kw">{esc}</span>')
            elif val in _CPP_CONST:
                out.append(f'<span class="tok-const">{esc}</span>')
            elif m.end() < len(text) and text[m.end()] == "(":  # call site
                out.append(f'<span class="tok-fn">{esc}</span>')
            else:
                out.append(esc)
        else:
            out.append(esc)
    return "".join(out)


def highlight_block(text: str, lang: str) -> str:
    """Highlight a whole multi-line snippet for `lang` ('purr' or 'cpp')."""
    line = _highlight_cpp_line if lang == "cpp" else highlight_code
    return "\n".join(line(ln) for ln in text.split("\n"))

# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

class Member:
    __slots__ = ("id", "kind", "name", "type_xml", "args", "brief_xml",
                 "detail_xml", "static", "prot", "compound",
                 "srcfile", "srcline")

class Compound:
    __slots__ = ("refid", "kind", "name", "short", "brief_xml", "detail_xml",
                 "members", "title")

def text_of(el) -> str:
    return "".join(el.itertext()) if el is not None else ""

def load() -> tuple[dict[str, Compound], dict[str, str]]:
    """Parse index.xml + every compound; return {refid: Compound} and a
    {member_id: compound_refid} index for resolving cross-references."""
    index = ET.parse(XML / "index.xml").getroot()
    # Public reference surface: modules (namespaces) + classes + the homepage.
    # C++ implementation detail — the linalg result structs and the template
    # concepts — is intentionally NOT rendered (refs to them degrade to plain text).
    wanted = {NS, CLASS, "page"}
    compounds: dict[str, Compound] = {}
    member_index: dict[str, str] = {}

    for c in index.findall("compound"):
        kind = c.get("kind")
        if kind not in wanted:
            continue
        refid = c.get("refid")
        cdef = ET.parse(XML / f"{refid}.xml").getroot().find("compounddef")
        if cdef is None:
            continue
        # A per-module README is diverted into its module page (see render_compound_page),
        # not rendered as its own page.
        if kind == "page":
            loc = cdef.find("location")
            mm = _MOD_README_RE.search(loc.get("file") if loc is not None else "")
            if mm:
                MODULE_README[mm.group(1)] = cdef.find("detaileddescription")
                continue
        name = cdef.findtext("compoundname") or c.findtext("name") or refid
        # Skip internal namespaces: anonymous (`anonymous_namespace{...}`) and `detail`.
        segs = name.split("::")
        if kind == NS and ("detail" in segs or any("anonymous" in s or s.startswith("@") for s in segs)):
            continue
        comp = Compound()
        comp.refid = refid
        comp.kind = kind
        comp.name = name
        comp.short = comp.name.replace("cheatah::", "")
        comp.title = cdef.findtext("title") or comp.short
        comp.brief_xml = cdef.find("briefdescription")
        comp.detail_xml = cdef.find("detaileddescription")
        comp.members = []
        if kind == NS:
            MOD_SHORT[refid] = comp.short.replace("::", ".")
        for md in cdef.iter("memberdef"):
            m = Member()
            m.id = md.get("id")
            m.kind = md.get("kind")
            m.static = md.get("static") == "yes"
            m.prot = md.get("prot")
            m.name = md.findtext("name") or ""
            m.type_xml = md.find("type")
            m.args = (md.findtext("argsstring") or "").strip()
            m.brief_xml = md.find("briefdescription")
            m.detail_xml = md.find("detaileddescription")
            m.compound = refid
            loc = md.find("location")
            # Prefer the definition (bodyfile); fall back to the declaration.
            m.srcfile = (loc.get("bodyfile") or loc.get("declfile") or loc.get("file")) if loc is not None else None
            m.srcline = (loc.get("bodystart") or loc.get("declline") or loc.get("line")) if loc is not None else None
            comp.members.append(m)
            member_index[m.id] = refid
        if comp.kind == NS and not comp.members:
            continue   # drop empty container namespaces (e.g. the root `cheatah`)
        compounds[refid] = comp
    return compounds, member_index


# ---------------------------------------------------------------------------
# Description renderer: Doxygen detaileddescription XML -> HTML
# ---------------------------------------------------------------------------

class Renderer:
    SECT_TITLES = {"return": "Returns", "note": "Note", "see": "See also",
                   "warning": "Warning", "remark": "Remark", "attention": "Attention",
                   "pre": "Precondition", "post": "Postcondition", "par": ""}
    # @complexity / @alloc / @test / @crtest / @systest (+ todo/bug) arrive as
    # <xrefsect>; map the xref title to a CSS class.
    XREF_CLASS = {"Complexity": "complexity", "Allocation": "alloc", "Test": "test",
                  "Compile-run test": "crtest", "System test": "systest",
                  "Todo": "todo", "Bug": "bug"}
    # Friendlier display labels for the test chips.
    XREF_LABEL = {"Test": "Unit test"}
    # Xref titles whose body is a list of gtest names to linkify to test source.
    TEST_TITLES = {"Test", "Compile-run test", "System test"}

    def __init__(self, member_index: dict[str, str], modules: dict[str, str] | None = None):
        self.mi = member_index
        self.modules = modules or {}   # short module name -> compound refid (for table links)
        self.valid: set[str] | None = None   # refids that have a page; None => link everything
        self.src_map: dict[str, str] = {}    # source path -> source-page html filename
        self.test_index: dict[str, tuple[str, str]] = {}  # "Suite.Name" -> (path, line)
        # (module_short, func) -> set of system-test names that call the function.
        self.app_usage: dict[tuple[str, str], set[str]] = {}
        self.cur_module = ""   # set per page/member so @systest can list every test it's in
        self.cur_func = ""

    def src_link(self, path: str, line) -> str | None:
        page = self.src_map.get(path)
        if not page:
            return None
        return f"{page}#L{line}" if line else page

    def href(self, refid: str, kindref: str) -> str:
        if kindref == "compound":
            return f"{refid}.html"
        compound = self.mi.get(refid) or refid.rsplit("_1", 1)[0]
        return f"{compound}.html#{refid}"

    def inline(self, el) -> str:
        """Render the children (and text) of `el` as inline/flow HTML."""
        out = [html.escape(el.text)] if el.text else []
        for child in el:
            out.append(self.node(child))
            if child.tail:
                out.append(html.escape(child.tail))
        return "".join(out)

    def node(self, el) -> str:
        tag = el.tag
        fn = getattr(self, f"t_{tag}", None)
        if fn:
            return fn(el)
        # Unknown / passthrough: render inline.
        return self.inline(el)

    # Children that render as block-level (must not be wrapped in <p>).
    BLOCK = {"parameterlist", "simplesect", "xrefsect", "itemizedlist", "orderedlist",
             "variablelist", "table", "programlisting", "hruler", "sect1", "sect2", "sect3"}

    # --- block elements ---
    def t_para(self, el) -> str:
        out, buf = [], []
        def flush():
            s = "".join(buf).strip()
            if s:
                out.append(f"<p>{s}</p>")
            buf.clear()
        if el.text:
            buf.append(html.escape(el.text))
        for child in el:
            if child.tag in self.BLOCK:
                flush()
                out.append(self.node(child))
                if child.tail:
                    buf.append(html.escape(child.tail))
            else:
                buf.append(self.node(child))
                if child.tail:
                    buf.append(html.escape(child.tail))
        flush()
        return "".join(out)

    def t_parameterlist(self, el) -> str:
        rows = []
        for item in el.findall("parameteritem"):
            names = ", ".join(html.escape(text_of(n))
                              for n in item.findall(".//parametername"))
            desc = "".join(self.node(p) for p in item.find("parameterdescription"))
            rows.append(f'<tr><td class="pname"><code>{names}</code></td>'
                        f'<td class="pdesc">{desc}</td></tr>')
        kind = el.get("kind")
        title = "Template parameters" if kind == "templateparam" else "Parameters"
        return (f'<div class="params"><div class="params-h">{title}</div>'
                f'<table class="params-t">{"".join(rows)}</table></div>')

    def t_simplesect(self, el) -> str:
        kind = el.get("kind")
        title = self.SECT_TITLES.get(kind, kind.capitalize() if kind else "")
        body = "".join(self.node(c) for c in el)
        if kind == "return":
            return f'<div class="ret"><span class="ret-h">{title}</span> {body}</div>'
        cls = "admon " + (kind or "note")
        return (f'<div class="{cls}"><div class="admon-h">{title}</div>'
                f'<div class="admon-b">{body}</div></div>')

    def t_xrefsect(self, el) -> str:
        title = text_of(el.find("xreftitle")).strip()
        cls = self.XREF_CLASS.get(title, "xref")
        label = self.XREF_LABEL.get(title, title)
        if title == "System test":
            body = self.render_systests(text_of(el.find("xrefdescription")))
        elif title in self.TEST_TITLES:
            body = self.render_tests(text_of(el.find("xrefdescription")))
        else:
            body = "".join(self.node(c) for c in el.find("xrefdescription"))
        return (f'<div class="tag tag-{cls}"><span class="tag-k">{html.escape(label)}</span>'
                f'<span class="tag-v">{body}</span></div>')

    @staticmethod
    def _test_name_html(name: str) -> str:
        """Color a gtest `Suite.Case` like a highlighted qualified name (suite as a
        namespace, case as a member) so the @test rows read as code, not flat text."""
        if "." in name:
            suite, case = name.split(".", 1)
            return (f'<span class="tn-suite">{html.escape(suite)}</span>.'
                    f'<span class="tn-case">{html.escape(case)}</span>')
        return f'<span class="tn-case">{html.escape(name)}</span>'

    def link_test(self, name: str) -> str:
        """One gtest `Suite.Name` rendered as a link to its source (if known)."""
        inner = self._test_name_html(name)
        loc = self.test_index.get(name)
        if loc and loc[0] in self.src_map:
            return f'<a href="{self.src_link(loc[0], loc[1])}"><code>{inner}</code></a>'
        return f"<code>{inner}</code>"

    def render_tests(self, text: str) -> str:
        """Render @test/@crtest value(s) as links to the test's source location."""
        names = [n for n in re.split(r"[,\s]+", text.strip()) if n]
        return " ".join(self.link_test(n) for n in names)

    MAX_SYSTESTS = 3  # show this many, then "…" — every system test the function is in

    def render_systests(self, text: str) -> str:
        """List EVERY system test the current function appears in: the per-module
        @systest (from the tag) plus any cross-module app that calls it. Truncate
        with a "…" (full list in its title) when too many to render cleanly."""
        names = [n for n in re.split(r"[,\s]+", text.strip()) if n]
        for extra in sorted(self.app_usage.get((self.cur_module, self.cur_func), ())):
            if extra not in names:
                names.append(extra)
        shown = [self.link_test(n) for n in names[:self.MAX_SYSTESTS]]
        if len(names) > self.MAX_SYSTESTS:
            rest = ", ".join(names[self.MAX_SYSTESTS:])
            shown.append(f'<span class="more" title="{html.escape(rest)}">…</span>')
        return " ".join(shown)

    def t_itemizedlist(self, el) -> str:
        items = "".join(f"<li>{self.inline(li)}</li>" for li in el.findall("listitem"))
        return f"<ul>{items}</ul>"

    def t_orderedlist(self, el) -> str:
        items = "".join(f"<li>{self.inline(li)}</li>" for li in el.findall("listitem"))
        return f"<ol>{items}</ol>"

    def t_variablelist(self, el) -> str:
        # Pairs of <varlistentry><term/></varlistentry><listitem/>
        out = ["<dl class='vlist'>"]
        for child in el:
            if child.tag == "varlistentry":
                out.append(f"<dt>{self.inline(child.find('term'))}</dt>")
            elif child.tag == "listitem":
                out.append(f"<dd>{''.join(self.node(c) for c in child)}</dd>")
        out.append("</dl>")
        return "".join(out)

    # Verdict phrases in any performance table get the warm green/red treatment:
    # "cheatah 2.8×" / "N× faster" → green; "NumPy 1.1×" / "Eigen 1.5×" / "N× slower" → red.
    _PERF_UP_RE = re.compile(r"(cheatah\s+[\d.]+×|≈?[\d.]+×\s+faster)")
    _PERF_DOWN_RE = re.compile(r"(NumPy\s+[\d.]+×|Eigen\s+[\d.]+×|≈?[\d.]+×\s+slower)")

    @classmethod
    def _color_verdict(cls, inner: str) -> str:
        inner = cls._PERF_UP_RE.sub(r'<span class="perf-faster">\1</span>', inner)
        inner = cls._PERF_DOWN_RE.sub(r'<span class="perf-slower">\1</span>', inner)
        return inner

    def t_table(self, el) -> str:
        rows = []
        for r in el.findall("row"):
            cells = []
            for c in r.findall("entry"):
                tag = "th" if c.get("thead") == "yes" else "td"
                inner = "".join(self.node(p) for p in c)
                # Linkify a cell whose text is a module name (e.g. the homepage table).
                key = text_of(c).strip()
                if tag == "td" and key in self.modules:
                    inner = f'<a href="{self.modules[key]}.html" class="mod-link">{inner}</a>'
                elif tag == "td":
                    inner = self._color_verdict(inner)
                cells.append(f"<{tag}>{inner}</{tag}>")
            rows.append(f"<tr>{''.join(cells)}</tr>")
        return f'<table class="dtable">{"".join(rows)}</table>'

    def _code_text(self, el) -> str:
        # Doxygen encodes EVERY space inside a code block as a <sp/> element (with an
        # optional `value` = run length), which itertext() would silently drop and
        # run the tokens together. Walk the tree and turn each <sp/> back into spaces
        # so indentation and alignment survive.
        out = []
        if el.text:
            out.append(el.text)
        for child in el:
            if child.tag == "sp":
                n = child.get("value")
                out.append(" " * (int(n) if n and n.isdigit() else 1))
            else:
                out.append(self._code_text(child))
            if child.tail:
                out.append(child.tail)
        return "".join(out)

    def t_programlisting(self, el) -> str:
        lines = []
        for codeline in el.findall("codeline"):
            lines.append(highlight_code(self._code_text(codeline)))
        return f'<pre class="code"><code>{chr(10).join(lines)}</code></pre>'

    def t_sect1(self, el) -> str:
        title = el.findtext("title") or ""
        body = "".join(self.node(c) for c in el if c.tag != "title")
        h = f"<h2>{html.escape(title)}</h2>" if title else ""
        return h + body

    t_sect2 = t_sect1
    t_sect3 = t_sect1

    def t_hruler(self, el) -> str:
        return "<hr>"

    def t_linebreak(self, el) -> str:
        return "<br>"

    # --- inline elements ---
    def t_computeroutput(self, el) -> str:
        # Inline `code` (backticks, incl. in guide tables) gets the same token
        # highlighting as block code — as long as it's plain text (only <sp/> spacing
        # children, no nested links/refs, which would need inline() rendering).
        if all(c.tag == "sp" for c in el):
            return f"<code>{highlight_code(self._code_text(el))}</code>"
        return f"<code>{self.inline(el)}</code>"

    def t_bold(self, el) -> str:
        return f"<strong>{self.inline(el)}</strong>"

    def t_emphasis(self, el) -> str:
        return f"<em>{self.inline(el)}</em>"

    def t_ref(self, el) -> str:
        refid = el.get("refid", "")
        kindref = el.get("kindref", "compound")
        target = refid if kindref == "compound" else self.mi.get(refid)
        if self.valid is not None and target not in self.valid:
            return self.inline(el)   # target has no page -> render as plain text, not a dead link
        return f'<a href="{self.href(refid, kindref)}">{self.inline(el)}</a>'

    def t_ulink(self, el) -> str:
        return f'<a href="{html.escape(el.get("url",""))}" target="_blank" rel="noopener">{self.inline(el)}</a>'

    def t_anchor(self, el) -> str:
        return el.tail and "" or ""   # anchors carry no visible content

    def t_parametername(self, el) -> str:
        return self.inline(el)

    def render(self, el) -> str:
        if el is None:
            return ""
        return "".join(self.node(c) for c in el)

    def brief(self, el) -> str:
        """Brief description as inline HTML (no wrapping <p>)."""
        if el is None:
            return ""
        para = el.find("para")
        return self.inline(para) if para is not None else ""


# ---------------------------------------------------------------------------
# HTML page assembly
# ---------------------------------------------------------------------------

KIND_BADGE = {"function": "fn", "variable": "var", "typedef": "type",
              "enum": "enum", "friend": "friend"}

def member_signature(r: Renderer, m: Member) -> str:
    typ = r.inline(m.type_xml) if m.type_xml is not None else ""
    args = html.escape(m.args)
    static = '<span class="kw">static</span> ' if m.static else ""
    return f'{static}<span class="mtype">{typ}</span> <span class="mname">{html.escape(m.name)}</span>{args}'

def render_member(r: Renderer, m: Member) -> str:
    badge = KIND_BADGE.get(m.kind, m.kind)
    r.cur_func = m.name   # so @systest can list every system test this function is in
    brief = r.brief(m.brief_xml)
    detail = r.render(m.detail_xml) + perf_row(m.compound, m.name)
    src = r.src_link(m.srcfile, m.srcline) if m.srcfile else None
    src_html = f'<a class="src-link" href="{src}" title="View source">source</a>' if src else ""
    return f"""<section class="member" id="{m.id}">
  <div class="member-head">
    <span class="badge badge-{badge}">{badge}</span>
    <code class="sig">{member_signature(r, m)}</code>
    <span class="member-actions">{src_html}<a class="anchor" href="#{m.id}" aria-label="permalink">#</a></span>
  </div>
  {f'<p class="member-brief">{brief}</p>' if brief else ''}
  <div class="member-body">{detail}</div>
</section>"""

# A guide can place two code blocks side by side: a paragraph `SIDEBYSIDE: left | right`
# immediately followed by two fenced code blocks becomes a two-column grid with those
# labels (reusing the transpiler page's .xpile styling). Raw HTML in a markdown page is
# stripped by Doxygen, so this marker-in-prose approach is what survives.
_SXS_RE = re.compile(
    r'<p>\s*SIDEBYSIDE:\s*([^|<]+?)\s*\|\s*([^<]+?)\s*</p>\s*'
    r'(<pre class="code"><code>.*?</code></pre>)\s*'
    r'(<pre class="code"><code>.*?</code></pre>)',
    re.S)

def apply_sidebyside(content: str) -> str:
    def pane(label: str, code: str) -> str:
        return (f'<figure class="xpile-pane">{code}<figcaption>'
                f'<span class="fname">{html.escape(label)}</span></figcaption></figure>')
    return _SXS_RE.sub(
        lambda m: f'<div class="xpile">{pane(m.group(1), m.group(3))}{pane(m.group(2), m.group(4))}</div>',
        content)

def page_shell(title: str, sidebar: str, content: str, toc: str, depth_ok=True) -> str:
    content = content.replace("🐾", '<span class="paw">🐾</span>')  # recolor blue system paw emoji
    content = apply_sidebyside(content)
    return f"""<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="generator" content="cheatah">
<title>{html.escape(title)} · cheatah</title>
<link rel="icon" href="cheatah-logo.png" type="image/png">
<link rel="stylesheet" href="cheatah-docs.css?v={VERS.get('css','')}">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="topbar">
  <div class="topbar-inner">
    <a class="brand" href="index.html"><img src="cheatah-logo.png" alt=""><span>cheatah</span></a>
    <div class="search"><input id="q" type="search" placeholder="Search the standard library…" autocomplete="off" spellcheck="false">
      <div id="results" class="results" hidden></div>
    </div>
    <button id="theme" class="theme-btn" title="Toggle theme" aria-label="Toggle theme">◐</button>
  </div>
</header>
<div class="layout">
  <nav class="sidebar" aria-label="Modules">{sidebar}</nav>
  <main id="main" class="content">{content}</main>
  <aside class="toc" aria-label="On this page">{toc}</aside>
</div>
<script src="search-index.js?v={VERS.get('search','')}"></script>
<script src="cheatah-docs.js?v={VERS.get('js','')}"></script>
<footer class="sitefoot">© 2026 BigBrain LLC — cheatah is free software under the MIT License. Lead engineer &amp; producer: Joshua Doucette, on behalf of BigBrain LLC.</footer>
</body>
</html>"""

def build_sidebar(compounds: dict[str, Compound]) -> str:
    parts = ['<a class="side-home" href="index.html">Overview</a>']
    # Guide pages, in a deliberate reading order. The two language-comparison pages
    # (cheatah ↔ Python, cheatah ↔ C++) live in their OWN "Language parity" section.
    GUIDE_ORDER = {"why": 0, "getting-started": 1, "porting": 3,
                   "performance": 4, "optimizations": 5, "security": 6, "md_CHANGELOG": 8}
    # "Essential tools": the runtime, the compiler, the package manager, import resolution.
    TOOLS_ORDER = {"runtime": 0, "purrc": 1, "biome": 2, "imports": 3}
    PARITY_ORDER = {"md_compiler_2PYTHON": 0, "cpp": 1}   # cheatah ↔ Python, then ↔ C++
    all_guides = [c for c in compounds.values()
                  if c.kind == "page" and c.refid not in XREF_PAGES]
    tools = sorted((c for c in all_guides if c.refid in TOOLS_ORDER),
                   key=lambda c: TOOLS_ORDER[c.refid])
    guides = sorted((c for c in all_guides
                     if c.refid not in PARITY_ORDER and c.refid not in TOOLS_ORDER),
                    key=lambda c: (GUIDE_ORDER.get(c.refid, 99), (c.title or c.short).lower()))
    parity = sorted((c for c in all_guides if c.refid in PARITY_ORDER),
                    key=lambda c: PARITY_ORDER[c.refid])

    # Essential tools — listed right after the Overview, before the Guides.
    if tools:
        tlinks = "".join(
            f'<li><a href="{c.refid}.html" data-ref="{c.refid}">{html.escape(c.title or c.short)}</a></li>'
            for c in tools)
        parts.append(
            f'<div class="side-group"><div class="side-h">Essential tools</div><ul>{tlinks}</ul></div>')

    # Guides — the Transpiler guide is generated by us (not a Doxygen @page), so inject it
    # into the reading order by hand, right before performance.
    links = []
    for c in guides:
        if c.refid in ("performance", "security", "md_CHANGELOG") and \
                not any('href="transpiler.html"' in l for l in links):
            links.append('<li><a href="transpiler.html" data-ref="transpiler">Transpiler</a></li>')
        links.append(
            f'<li><a href="{c.refid}.html" data-ref="{c.refid}">{html.escape(c.title or c.short)}</a></li>')
    if not any('href="transpiler.html"' in l for l in links):
        links.append('<li><a href="transpiler.html" data-ref="transpiler">Transpiler</a></li>')
    parts.append(f'<div class="side-group"><div class="side-h">Guides</div><ul>{"".join(links)}</ul></div>')

    # Language parity — cheatah ↔ Python and cheatah ↔ C++.
    if parity:
        plinks = "".join(
            f'<li><a href="{c.refid}.html" data-ref="{c.refid}">{html.escape(c.title or c.short)}</a></li>'
            for c in parity)
        parts.append(
            f'<div class="side-group"><div class="side-h">Language parity</div><ul>{plinks}</ul></div>')

    # Modules — top-level modules, with submodules (os.path, parsers.json, …) nested as
    # collapsible dropdowns. Classes are NOT a sidebar column; each module page lists its own.
    ns = [c for c in compounds.values() if c.kind == NS]
    kids: dict[str, list] = {}
    for c in ns:
        if "::" in c.short:
            kids.setdefault(c.short.rsplit("::", 1)[0], []).append(c)

    def node(c: Compound) -> str:
        leaf = c.short.rsplit("::", 1)[-1]
        link = f'<a href="{c.refid}.html" data-ref="{c.refid}">{html.escape(leaf)}</a>'
        children = sorted(kids.get(c.short, []), key=lambda k: k.short.lower())
        if children:
            inner = "".join(f'<li>{node(k)}</li>' for k in children)
            return f'<details class="side-sub"><summary>{link}</summary><ul>{inner}</ul></details>'
        return link

    tops = sorted((c for c in ns if "::" not in c.short), key=lambda c: c.short.lower())
    mod = "".join(f'<li class="side-mod">{node(c)}</li>' for c in tops)
    parts.append(f'<div class="side-group"><div class="side-h">Modules</div><ul>{mod}</ul></div>')
    return "".join(parts)

def build_toc(members: list[Member]) -> str:
    if not members:
        return ""
    funcs = [m for m in members if m.kind == "function"]
    others = [m for m in members if m.kind != "function"]
    parts = ['<div class="toc-h">On this page</div><ul>']
    for m in funcs + others:
        parts.append(f'<li><a href="#{m.id}">{html.escape(m.name)}</a></li>')
    parts.append("</ul>")
    return "".join(parts)

def render_compound_page(r: Renderer, comp: Compound, sidebar: str) -> str:
    # A standalone content page (a Doxygen markdown @page, e.g. Performance) renders
    # as prose with its title; reference compounds render as a code-named symbol.
    is_page = comp.kind == "page"
    kind_label = {NS: "Module", CLASS: "Class", STRUCT: "Struct",
                  CONCEPT: "Concept"}.get(comp.kind, "Guide")
    r.cur_module = comp.short   # module context for @systest app-usage lookup
    brief = r.brief(comp.brief_xml)
    # A module page leads with its README.md (examples + prose), then any namespace
    # doc block, then the auto-generated reference for its members.
    readme = MODULE_README.get(comp.short) if comp.kind == NS else None
    detail = r.render(readme) + r.render(comp.detail_xml)
    # Group members.
    funcs = [m for m in comp.members if m.kind == "function"]
    vars_ = [m for m in comp.members if m.kind == "variable"]
    types = [m for m in comp.members if m.kind in ("typedef", "enum")]
    sections = []
    for label, items in (("Functions", funcs), ("Constants & variables", vars_), ("Types", types)):
        if not items:
            continue
        body = "".join(render_member(r, m) for m in items)
        sections.append(f'<h2 class="group">{label}</h2>{body}')
    # A module page lists its own classes (linking to each class's page) rather than the
    # site carrying a separate "Classes" sidebar column.
    class_section = ""
    if comp.kind == NS and MODULE_CLASSES.get(comp.short):
        items = "".join(
            f'<li><a href="{c.refid}.html"><code>{html.escape(c.short.rsplit("::", 1)[-1])}</code></a>'
            + (f' — {r.brief(c.brief_xml)}' if r.brief(c.brief_xml) else '') + '</li>'
            for c in MODULE_CLASSES[comp.short])
        class_section = f'<h2 class="group">Classes</h2><ul class="class-list">{items}</ul>'
    title = html.escape(comp.title) if is_page else f"<code>{html.escape(comp.short)}</code>"
    content = f"""<div class="page-head">
  <div class="eyebrow">{kind_label}</div>
  <h1>{title}</h1>
  {f'<p class="lede">{brief}</p>' if brief else ''}
</div>
<div class="overview">{detail}</div>
{class_section}
{"".join(sections)}"""
    toc = build_toc(comp.members)
    return page_shell(comp.title if is_page else comp.short, sidebar, content, toc)

def render_home(r: Renderer, comp: Compound, sidebar: str) -> str:
    detail = r.render(comp.detail_xml)
    content = f"""<div class="home">
  <div class="hero">
    <img class="hero-logo" src="cheatah-logo.png" alt="">
    <div class="hero-text">
      <h1 class="hero-title">cheatah</h1>
      <p class="hero-tagline">Programs so fast they purrrrrrrrrrrrr like a kitten.</p>
    </div>
  </div>
  {detail}</div>"""
    return page_shell("cheatah standard library", sidebar, content, "")

TEST_RE = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")

def build_test_index() -> dict[str, tuple[str, str]]:
    """Map gtest `Suite.Name` -> (repo-relative path, line) by scanning the tests:
    the in-process unit tests (stdlib/tests) and the compile-run + system-level
    tests (tests/purrc)."""
    idx: dict[str, tuple[str, str]] = {}
    for tdir in (ROOT / "stdlib" / "tests", ROOT / "tests" / "purrc"):
        if not tdir.is_dir():
            continue
        for f in sorted(tdir.glob("*.cpp")):
            rel = str(f.relative_to(ROOT))
            for i, line in enumerate(f.read_text().splitlines(), 1):
                m = TEST_RE.search(line)
                if m:
                    idx[f"{m.group(1)}.{m.group(2)}"] = (rel, str(i))
    return idx

_CALL_RE = re.compile(r"((?:\w+\.)*)(\w+)\s*\(")
_QUAL_MODULE = {
    "": "builtins", "math": "math", "io": "io", "string": "string",
    "ndarray": "ndarray", "linalg": "linalg", "os": "os", "os.path": "os::path",
    "socket": "socket", "statistics": "statistics", "random": "random",
    "hashlib": "hashlib", "datetime": "datetime", "time": "time",
}

def build_app_usage() -> dict[tuple[str, str], set[str]]:
    """Scan the cross-module app system tests (tests/purrc/app_*_test.cpp) and map
    (module_short, function) -> {system-test names that call it}, so the docs can
    list every system test a function actually appears in."""
    usage: dict[tuple[str, str], set[str]] = {}
    tdir = ROOT / "tests" / "purrc"
    if not tdir.is_dir():
        return usage
    for f in sorted(tdir.glob("app_*_test.cpp")):
        text = f.read_text()
        tm = TEST_RE.search(text)
        if not tm:
            continue
        test_name = f"{tm.group(1)}.{tm.group(2)}"
        pm = re.search(r'R"PURR\((.*?)\)PURR"', text, re.S)
        purr = pm.group(1) if pm else ""
        for qual, func in _CALL_RE.findall(purr):
            mod = _QUAL_MODULE.get(qual.rstrip("."))
            if mod is not None:
                usage.setdefault((mod, func), set()).add(test_name)
    return usage

def src_page_name(rel_path: str) -> str:
    return "src_" + re.sub(r"[^A-Za-z0-9]", "_", rel_path) + ".html"

def render_source_page(rel_path: str, sidebar: str) -> str:
    lines = (ROOT / rel_path).read_text(errors="replace").splitlines()
    rows = []
    for i, line in enumerate(lines, 1):
        rows.append(f'<div class="srcline" id="L{i}">'
                    f'<a class="lnno" href="#L{i}">{i}</a>'
                    f'<code class="lc">{html.escape(line)}</code></div>')
    content = (f'<div class="page-head"><div class="eyebrow">Source</div>'
               f'<h1><code>{html.escape(rel_path)}</code></h1></div>'
               f'<div class="srcview">{"".join(rows)}</div>')
    return page_shell(rel_path + " · source", sidebar, content, "")

# The transpiler page: the `gen/` examples shown .purr ↔ .gen.cpp side by side. Ordered
# simplest-first; the blurb mirrors gen/README.md so the two stay in sync by eye.
TRANSPILER_EXAMPLES = [
    ("fizzbuzz", "Ranges, <code>if</code>, floor division <code>//</code>, and string building."),
    ("http_response", "A <code>+</code>-chain self-append lowers to one chained "
                      "<code>((head += a) += b) += c;</code> — no temporary per piece."),
    ("grades", "A <code>struct</code> with a method, a <code>list</code>, and method-call "
               "(UFCS) syntax."),
    ("palette", "A scoped <code>enum</code> with <code>match</code>/<code>case</code> — the "
                "<code>enum class</code> lowering, with a generated <code>operator&lt;&lt;</code>."),
    ("vectors", "<code>ndarray</code> arrays through <code>linalg</code> routines — the numeric "
                "core, with per-module namespace aliases (<code>ndarray::</code>, "
                "<code>linalg::</code>)."),
    ("numerics", "Four modules linked into one program: <code>math</code>, <code>ndarray</code> "
                 "and <code>linalg</code> each lower to an <code>#include</code> and a short "
                 "namespace alias, solving a 3×3 system on the SIMD core."),
    ("shapes", "An <code>interface</code> lowers to a C++20 <b>concept</b> (a "
               "<code>static_assert</code> checks every <code>struct</code> that fulfills it), and "
               "an interface-typed parameter constrains the generated template — no inheritance."),
]

def _xpile_pane(fname: str, lang_label: str, code_html: str) -> str:
    return (f'<figure class="xpile-pane">'
            f'<pre class="code"><code>{code_html}</code></pre>'
            f'<figcaption><span class="fname">{html.escape(fname)}</span>'
            f'<span class="xpile-lang">{html.escape(lang_label)}</span></figcaption></figure>')

def render_transpiler_page(sidebar: str) -> str:
    gen_dir = ROOT / "gen"
    sections, toc_items = [], []
    for name, blurb in TRANSPILER_EXAMPLES:
        purr_f, cpp_f = gen_dir / f"{name}.purr", gen_dir / f"{name}.gen.cpp"
        if not (purr_f.is_file() and cpp_f.is_file()):
            continue
        purr_html = highlight_block(purr_f.read_text().rstrip("\n"), "purr")
        cpp_html = highlight_block(cpp_f.read_text().rstrip("\n"), "cpp")
        sections.append(
            f'<section class="xpile-ex" id="{name}">'
            f'<h2 class="group"><code>{name}.purr</code></h2>'
            f'<p class="xpile-blurb">{blurb}</p>'
            f'<div class="xpile">'
            f'{_xpile_pane(f"{name}.purr", "cheatah", purr_html)}'
            f'{_xpile_pane(f"{name}.gen.cpp", "C++", cpp_html)}'
            f'</div></section>')
        toc_items.append(f'<li><a href="#{name}">{html.escape(name)}</a></li>')
    intro = (
        "<p><code>purrc</code> is a <strong>transpiler</strong>: it compiles a "
        "<code>.purr</code> program to modern C++ — the <code>.gen.cpp</code> shown on "
        "the right — which is then built at <code>-O3 -march=native</code> into a loadable "
        "module the <code>cheatah</code> runtime runs. The whole program is emitted inside a "
        "<code>namespace cheatah_program</code>, where each module gets its own short alias "
        "(<code>io::</code>, <code>ndarray::</code>, …) and the exported entry point is a "
        "one-line <code>extern \"C\"</code> trampoline.</p>"
        "<p>These are the programs in the repository's <code>gen/</code> folder; regenerate "
        "them with <code>bash gen/generate.sh</code> to see how a language change moves the "
        "output.</p>")
    content = (f'<div class="page-head"><div class="eyebrow">Guide</div>'
               f'<h1>Transpiler</h1>'
               f'<p class="lede">What <code>purrc</code> emits — every <code>gen/</code> '
               f'example, <code>.purr</code> beside its generated C++.</p></div>'
               f'<div class="overview">{intro}</div>{"".join(sections)}')
    toc = ('<div class="toc-h">On this page</div><ul>' + "".join(toc_items) + "</ul>"
           if toc_items else "")
    return page_shell("Transpiler", sidebar, content, toc)

def build_search_index(compounds: dict[str, Compound]) -> str:
    entries = []
    for c in compounds.values():
        if c.kind == "page":
            continue
        entries.append({"n": c.short, "k": c.kind, "u": f"{c.refid}.html"})
        for m in c.members:
            entries.append({"n": m.name, "k": m.kind, "u": f"{c.refid}.html#{m.id}",
                            "c": c.short})
    return "window.SEARCH=" + json.dumps(entries, separators=(",", ":")) + ";"


def main() -> int:
    if not XML.is_dir():
        print(f"generate: {XML} not found — run Doxygen with GENERATE_XML=YES first")
        return 1
    compounds, member_index = load()
    modules = {c.short: c.refid for c in compounds.values() if c.kind == NS}
    r = Renderer(member_index, modules)
    r.valid = set(compounds.keys())   # only link to compounds that actually have a page

    # Source pages: every file a member is defined in, plus the test sources.
    src_files: set[str] = set()
    for comp in compounds.values():
        for m in comp.members:
            if m.srcfile:
                src_files.add(m.srcfile)
    for tdir in (ROOT / "stdlib" / "tests", ROOT / "tests" / "purrc"):
        if tdir.is_dir():
            src_files.update(str(f.relative_to(ROOT)) for f in tdir.glob("*.cpp"))
    src_map = {p: src_page_name(p) for p in sorted(src_files) if (ROOT / p).is_file()}
    r.src_map = src_map
    r.test_index = build_test_index()
    r.app_usage = build_app_usage()

    OUT.mkdir(parents=True, exist_ok=True)
    # Map each class to its owning module so the module page can list its classes
    # (e.g. "socket::Conn" -> module "socket"; "parsers::json::Array" -> "parsers::json").
    for c in compounds.values():
        if c.kind == CLASS and "::" in c.short:
            MODULE_CLASSES.setdefault(c.short.rsplit("::", 1)[0], []).append(c)
    for lst in MODULE_CLASSES.values():
        lst.sort(key=lambda c: c.short.lower())
    # Synthesize a module page for any path referenced as a submodule parent or a class owner
    # but lacking its own Doxygen namespace (e.g. "parsers" / "parsers::url" after the parsers
    # refactor), so the module tree + per-module class lists stay complete without a separate
    # Classes sidebar column.
    real_ns = {c.short for c in compounds.values() if c.kind == NS}
    needed: set[str] = set(MODULE_CLASSES.keys())
    for short in list(real_ns):
        parts = short.split("::")
        for i in range(1, len(parts)):
            needed.add("::".join(parts[:i]))
    for short in sorted(needed - real_ns):
        s = Compound()
        s.refid = "nsx_" + short.replace("::", "_")
        s.kind, s.name, s.short = NS, "cheatah::" + short, short
        s.title = short.replace("::", ".")
        s.brief_xml = s.detail_xml = None
        s.members = []
        compounds[s.refid] = s
        MOD_SHORT[s.refid] = short.replace("::", ".")
    sidebar = build_sidebar(compounds)

    # Content hashes for cache-busting (?v=…) so browsers never serve stale assets.
    search_js = build_search_index(compounds)
    VERS["css"] = _ver((ASSETS / "cheatah-docs.css").read_text())
    VERS["js"] = _ver((ASSETS / "cheatah-docs.js").read_text())
    VERS["search"] = _ver(search_js)

    pages = 0
    for rel_path, fname in src_map.items():
        (OUT / fname).write_text(render_source_page(rel_path, sidebar))
        pages += 1
    for comp in compounds.values():
        if comp.kind == "page":
            if comp.refid == "indexpage":
                (OUT / "index.html").write_text(render_home(r, comp, sidebar))
                pages += 1
            elif comp.refid not in XREF_PAGES:  # a real content/guide page (e.g. Performance)
                (OUT / f"{comp.refid}.html").write_text(render_compound_page(r, comp, sidebar))
                pages += 1
            continue
        (OUT / f"{comp.refid}.html").write_text(render_compound_page(r, comp, sidebar))
        pages += 1

    # The hand-built Transpiler guide (gen/*.purr beside their *.gen.cpp).
    (OUT / "transpiler.html").write_text(render_transpiler_page(sidebar))
    pages += 1

    (OUT / "search-index.js").write_text(search_js)
    # Copy static assets.
    for asset in ("cheatah-docs.css", "cheatah-docs.js"):
        shutil.copy(ASSETS / asset, OUT / asset)
    logo = ROOT / "docs" / "theme" / "cheatah-logo.png"
    if logo.exists():
        shutil.copy(logo, OUT / "cheatah-logo.png")

    print(f"generate: wrote {pages} pages + search index -> {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
