#!/usr/bin/env bash
# Build the cheatah docs site: run Doxygen, then recolor the residual blues to the
# warm earthy theme. Output: docs/html/ (serve it with the VS Code "Open docs in
# browser" task, or scripts/serve-docs.purr).
set -euo pipefail
cd "$(dirname "$0")/.."

DOXYGEN="${DOXYGEN:-doxygen}"
if ! command -v "$DOXYGEN" >/dev/null 2>&1; then
    # fall back to the pinned local install
    DOXYGEN="$HOME/Tools/doxygen-1.16.1/bin/doxygen"
fi

echo "[docs] doxygen…"
"$DOXYGEN" Doxyfile
echo "[docs] postprocess (recolor)…"
python3 docs/postprocess.py
echo "[docs] done -> docs/html/index.html"
