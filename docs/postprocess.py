#!/usr/bin/env python3
"""Post-process the generated Doxygen site to match the cheatah theme:
recolor the residual blues (the Graphviz diagrams and a few hardcoded hexes in
doxygen.css) to the warm, earthy palette. Run via docs/build-docs.sh."""
import re
import sys
from pathlib import Path

HTML = Path(__file__).resolve().parent / "html"

# Doxygen's blue diagram/UI hexes -> earthy tan / warm-dark replacements.
SVG_MAP = {
    "#63b8ff": "#d6ad73",   # primary node/edge blue -> tan
    "#a2b4d6": "#b99e78",   # secondary blue -> muted tan
    "#edf0f7": "#2a251d",   # near-white node fill -> warm dark
    "#4665a2": "#9a7c4f",
    "#1f3b5c": "#2a241b",
}
CSS_MAP = {
    "#1982d2": "#d6ad73",
    "#212f4b": "#38322a",
    "#86a9c4": "#cdb083",
    "#4779ac": "#b08a55",
    "#05070c": "#141110",
    "#090d16": "#16130f",
    "#0b101a": "#1b1814",
}

def recolor(text, mapping):
    n = 0
    for src, dst in mapping.items():
        pat = re.compile(re.escape(src), re.IGNORECASE)
        text, c = pat.subn(dst, text)
        n += c
    return text, n

def main():
    if not HTML.is_dir():
        print(f"postprocess: {HTML} not found — generate the site first (doxygen Doxyfile)", file=sys.stderr)
        return 1
    total = 0
    for svg in HTML.glob("*.svg"):
        text = svg.read_text()
        new, n = recolor(text, SVG_MAP)
        if n:
            svg.write_text(new)
            total += n
    css = HTML / "doxygen.css"
    if css.exists():
        text = css.read_text()
        new, n = recolor(text, CSS_MAP)
        if n:
            css.write_text(new)
            total += n
    print(f"postprocess: recolored {total} blue references → earthy palette")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
