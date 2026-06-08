#!/usr/bin/env python3
"""Generate the cheatah IntelliSense doc database from the Doxygen XML.

cheatah's stdlib is documented with Doxygen comments in the C++ headers, and
`docs/build-docs.sh` emits structured XML under `docs/xml/`. A cheatah call like
`math.sqrt(x)` maps 1:1 to `cheatah::math::sqrt` (codegen turns `module.fn` into
`cheatah::module::fn`), so we can build hover/completion docs straight from the
per-namespace XML.

Output: editors/vscode/data/functions.json — consumed by extension.js.

  python3 editors/vscode/scripts/gen-hover-docs.py

Re-run whenever the stdlib docs change (after `docs/build-docs.sh`).
"""
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
VSCODE = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(VSCODE))
XML_DIR = os.path.join(REPO, "docs", "xml")
OUT = os.path.join(VSCODE, "data", "functions.json")

# Doxygen mangles `::` to `_1_1`. We want the public module namespaces only —
# not the anonymous/hashed overload-set files and not `detail` helpers.
HASHED = re.compile(r"_0d[0-9a-f]+\.xml$")


def text_of(node):
    """Flatten an element's mixed content to a single trimmed string."""
    if node is None:
        return ""
    return re.sub(r"\s+", " ", "".join(node.itertext())).strip()


def module_name(compoundname):
    """`cheatah::os::path` -> `os.path`; `cheatah::builtins` -> '' (no prefix)."""
    parts = compoundname.split("::")
    if parts[0] != "cheatah":
        return None
    rest = parts[1:]
    if rest == ["builtins"]:
        return ""  # builtins are called bare: range(), len(), print()...
    if "detail" in rest:
        return None
    return ".".join(rest)


def parse_detail(detail):
    """Pull @param / @return text out of a <detaileddescription>."""
    params, returns = [], ""
    if detail is None:
        return params, returns
    for plist in detail.iter("parameterlist"):
        if plist.get("kind") != "param":
            continue
        for item in plist.findall("parameteritem"):
            names = [text_of(n) for n in item.iter("parametername")]
            desc = text_of(item.find("parameterdescription"))
            for nm in names:
                if nm:
                    params.append({"name": nm, "desc": desc})
    for sect in detail.iter("simplesect"):
        if sect.get("kind") == "return":
            returns = text_of(sect)
    return params, returns


# @xrefitem titles (Doxygen) -> short keys surfaced in the hover popup.
_XREF = {"Complexity": "complexity", "Allocation": "alloc", "Test": "test",
         "Compile-run test": "crtest", "System test": "systest"}


def parse_tags(detail):
    """Pull @complexity / @alloc / @test / @crtest / @systest out of a detail block."""
    tags = {}
    if detail is None:
        return tags
    for xs in detail.iter("xrefsect"):
        key = _XREF.get(text_of(xs.find("xreftitle")))
        if key and key not in tags:
            tags[key] = text_of(xs.find("xrefdescription"))
    return tags


def location_of(member):
    """Repo-relative header path + declaration line for control-click navigation."""
    loc = member.find("location")
    if loc is None:
        return None, None
    f = loc.get("declfile") or loc.get("file")
    ln = loc.get("declline") or loc.get("line")
    if f and os.path.isabs(f) and f.startswith(REPO + os.sep):
        f = os.path.relpath(f, REPO)
    return f, (int(ln) if ln else None)


# Language-level builtins that codegen synthesises directly (no stdlib symbol),
# so they have no Doxygen XML to harvest. Authored by hand here.
LANG_BUILTINS = [
    {
        "name": "range",
        "signature": "range(stop) | range(start, stop)",
        "brief": "Integer sequence for `for x in range(...)` loops.",
        "params": [
            {"name": "start", "desc": "inclusive lower bound (default 0)."},
            {"name": "stop", "desc": "exclusive upper bound."},
        ],
        "returns": "the half-open integer range [start, stop).",
    },
]

# cheatah keyword-spelled conversions that codegen maps to a `cheatah::builtins`
# symbol of a different name — alias the keyword onto the harvested doc.
BUILTIN_ALIASES = {"int": "to_int", "float": "to_float", "bool": "to_bool"}


def main():
    if not os.path.isdir(XML_DIR):
        sys.exit(f"no Doxygen XML at {XML_DIR} — run docs/build-docs.sh first")

    modules = {}  # module -> {function-name -> entry}
    for path in sorted(glob.glob(os.path.join(XML_DIR, "namespacecheatah*.xml"))):
        if HASHED.search(os.path.basename(path)):
            continue
        cd = ET.parse(path).getroot().find("compounddef")
        if cd is None:
            continue
        mod = module_name(cd.findtext("compoundname") or "")
        if mod is None:
            continue
        bucket = modules.setdefault(mod, {})
        for m in cd.iter("memberdef"):
            if m.get("kind") != "function" or m.get("prot") != "public":
                continue
            name = m.findtext("name") or ""
            if not name or name.startswith("operator") or name.startswith("~"):
                continue
            args = (m.findtext("argsstring") or "").strip()
            brief = text_of(m.find("briefdescription"))
            detail = m.find("detaileddescription")
            params, returns = parse_detail(detail)
            # Overloads collapse to one entry; keep the first that carries a brief.
            if name in bucket and not brief:
                continue
            srcfile, srcline = location_of(m)
            bucket[name] = {
                "name": name,
                "signature": f"{name}{args}",
                "brief": brief,
                "params": params,
                "returns": returns,
                "tags": parse_tags(detail),
                "srcfile": srcfile,
                "srcline": srcline,
            }

    # Alias keyword-spelled conversions onto their harvested builtin doc.
    builtins = modules.setdefault("", {})
    for alias, target in BUILTIN_ALIASES.items():
        if target in builtins and alias not in builtins:
            entry = dict(builtins[target])
            entry["name"] = alias
            entry["signature"] = alias + entry["signature"][len(target):]
            builtins[alias] = entry
    # Hand-authored language builtins (range, …).
    for entry in LANG_BUILTINS:
        builtins.setdefault(entry["name"], dict(entry))

    # Merge the periodic @perf benchmark numbers (docs/perf_data.json), keyed by the
    # same <module>.<name>, so the hover popup can show the speedup at a glance.
    perf, perf_meta = {}, {}
    perf_path = os.path.join(REPO, "docs", "perf_data.json")
    if os.path.exists(perf_path):
        pj = json.load(open(perf_path))
        perf, perf_meta = pj.get("functions", {}), pj.get("meta", {})

    # Flatten to a sorted list for stable diffs.
    out = []
    for mod in sorted(modules):
        for name in sorted(modules[mod]):
            e = dict(modules[mod][name])
            e["module"] = mod
            e["qualified"] = f"{mod}.{name}" if mod else name
            if e["qualified"] in perf:
                e["perf"] = perf[e["qualified"]]
            out.append(e)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(
            {"functions": out, "modules": sorted(m for m in modules if m),
             "perf_meta": perf_meta},
            f,
            indent=2,
            ensure_ascii=False,
        )
        f.write("\n")
    print(f"wrote {len(out)} functions across {len(modules)} modules -> {OUT}")


if __name__ == "__main__":
    main()
