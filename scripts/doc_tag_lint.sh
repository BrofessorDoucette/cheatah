#!/usr/bin/env bash
# Doc-TAG lint — the contract gate ABOVE doc_coverage.sh's presence gate.
#
# doc_coverage.sh proves every public entity has a brief + @param/@return. This script
# proves every public stdlib FUNCTION also carries the cheatah contract tags:
#     @complexity   the Big-O runtime of the call
#     @alloc        the heap behavior ("none." or exactly what it allocates)
#     @test/@crtest/@systest   at least one link to a real test
# (@warning and @concurrency stay judgment-applied — presence is not linted.)
#
# Method: a strict Doxygen run (same config as doc_coverage.sh, EXTRACT_ALL=NO so the
# Doxyfile's EXCLUDE_SYMBOLS/EXCLUDE_PATTERNS carve out internals) with XML on, into a
# temp dir; then walk every public <memberdef kind="function"> and check its
# detaileddescription for the xrefsect ids the Doxyfile ALIASES produce. Defaulted /
# deleted members are skipped (there is nothing to document about "= default").
#
#   scripts/doc_tag_lint.sh          # exit 1 listing every function missing a tag
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

DOXYGEN="${DOXYGEN:-doxygen}"
command -v "$DOXYGEN" >/dev/null 2>&1 || DOXYGEN="$HOME/Tools/doxygen-1.16.1/bin/doxygen"
command -v "$DOXYGEN" >/dev/null 2>&1 || { echo "doc-tag-lint: doxygen not found (install it, or set \$DOXYGEN)"; exit 1; }

XMLDIR="$(mktemp -d)"
trap 'rm -rf "$XMLDIR"' EXIT

( cat Doxyfile
  echo "EXTRACT_ALL=NO"
  echo "WARN_IF_UNDOCUMENTED=NO"
  echo "WARN_NO_PARAMDOC=NO"
  echo "GENERATE_HTML=NO"
  echo "GENERATE_LATEX=NO"
  echo "GENERATE_XML=YES"
  echo "XML_OUTPUT=$XMLDIR"
  echo "OUTPUT_DIRECTORY="
  echo "HAVE_DOT=NO"
  echo "QUIET=YES"
) | "$DOXYGEN" - >/dev/null 2>&1

python3 - "$XMLDIR" <<'PY'
import glob, os, sys
import xml.etree.ElementTree as ET

xmldir = sys.argv[1]

def text_of(el):
    return "".join(el.itertext()) if el is not None else ""

# Doxygen emits SEPARATE memberdefs for a header declaration, its out-of-line .cpp
# definition, and each explicit template instantiation. The contract tags live on the
# (one) documented declaration, so entities are folded by their template-stripped
# qualified name: a function passes if ANY of its memberdefs carries the full tag set.
groups = {}   # base name -> {"tags": set(), "sites": [(file, line)]}
for path in glob.glob(os.path.join(xmldir, "*.xml")):
    base = os.path.basename(path)
    if base in ("index.xml", "Doxyfile.xml") or base.startswith(("md_", "changelog", "indexpage")):
        continue
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        continue
    for member in root.iter("memberdef"):
        if member.get("kind") != "function" or member.get("prot") != "public":
            continue
        args = text_of(member.find("argsstring"))
        if "=delete" in args.replace(" ", "") or "=default" in args.replace(" ", ""):
            continue
        loc = member.find("location")
        file = loc.get("file") if loc is not None else "?"
        # Only lint the stdlib surface (markdown pages and examples produce members too).
        if "stdlib/" not in file:
            continue
        line = loc.get("line") if loc is not None else "?"
        name = text_of(member.find("qualifiedname")) or text_of(member.find("name"))
        name = name.split("<")[0].strip()   # fold explicit instantiations onto the primary
        detail = member.find("detaileddescription")
        ids = {x.get("id", "").split("_1")[0] for x in (detail.iter("xrefsect") if detail is not None else [])}
        g = groups.setdefault(name, {"tags": set(), "sites": []})
        g["tags"] |= ids
        g["sites"].append((file, line))

missing = []
for name, g in groups.items():
    lacks = []
    if "complexity" not in g["tags"]:
        lacks.append("@complexity")
    if "alloc" not in g["tags"]:
        lacks.append("@alloc")
    if not g["tags"] & {"test", "crtest", "systest"}:
        lacks.append("@test|@crtest|@systest")
    if lacks:
        file, line = sorted(g["sites"])[0]
        missing.append(f"  {file}:{line}  {name}  — missing {', '.join(lacks)}")

if missing:
    print(f"doc-tag-lint: FAIL — {len(missing)} of {len(groups)} public stdlib functions missing contract tags:")
    print("\n".join(sorted(set(missing))))
    sys.exit(1)
print(f"doc-tag-lint: 100% — all {len(groups)} public stdlib functions carry @complexity, @alloc, and a test tag.")
PY
