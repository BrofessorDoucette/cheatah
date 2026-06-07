#!/usr/bin/env bash
# Build the cheatah docs site. Doxygen is used ONLY as the C++ parser: it emits
# XML (docs/xml), and our own generator (docs/gen/generate.py) renders the modern
# site into docs/html. Serve it with the VS Code "Open docs in browser" task or
# scripts/serve-docs.purr.
set -euo pipefail
cd "$(dirname "$0")/.."

DOXYGEN="${DOXYGEN:-doxygen}"
if ! command -v "$DOXYGEN" >/dev/null 2>&1; then
    # fall back to the pinned local install
    DOXYGEN="$HOME/Tools/doxygen-1.16.1/bin/doxygen"
fi

echo "[docs] doxygen (XML)…"
"$DOXYGEN" Doxyfile

echo "[docs] generating site (docs/gen/generate.py)…"
rm -rf docs/html
python3 docs/gen/generate.py

echo "[docs] done -> docs/html/index.html"
