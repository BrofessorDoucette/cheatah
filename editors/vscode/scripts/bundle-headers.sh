#!/usr/bin/env bash
# Copy the stdlib C++ headers into editors/vscode/headers/ (preserving the
# stdlib/<module>/<file>.hpp layout that functions.json's `srcfile` paths use), so
# the VS Code extension's Go-to-Definition works without the cheatah repo open.
set -euo pipefail
cd "$(dirname "$0")/../../.."          # repo root
DEST="editors/vscode/headers"
rm -rf "$DEST"; mkdir -p "$DEST"
find stdlib -name '*.hpp' -exec cp --parents {} "$DEST/" \;
echo "bundled $(find "$DEST" -name '*.hpp' | wc -l) stdlib headers -> $DEST"
