#!/usr/bin/env bash
# Package the cheatah VS Code extension with the current hover DB + bundled headers,
# then install it into VS Code (replacing any older version). Best-effort: no-ops with
# a notice if `code` or vsce are unavailable (e.g. headless CI).
set -euo pipefail
cd "$(dirname "$0")/../../.."          # repo root
python3 editors/vscode/scripts/gen-hover-docs.py >/dev/null
bash editors/vscode/scripts/bundle-headers.sh
if ! command -v code >/dev/null 2>&1; then
    echo "install-extension: 'code' CLI not found — skipping editor install."; exit 0
fi
( cd editors/vscode && npx --yes @vscode/vsce package --skip-license --allow-missing-repository \
    -o cheatah.vsix >/dev/null )
code --install-extension editors/vscode/cheatah.vsix --force
echo "installed cheatah extension $(grep -o '\"version\": \"[^\"]*\"' editors/vscode/package.json | head -1)"
