#!/usr/bin/env bash
# Biome Standard drift check — the canonical standards/*.toml files must be
# byte-identical to what biome's in-source table renders (`biome standards
# --emit-toml <ver>`). The table ships inside biome so every cheatah release
# knows its standards; the TOML files are the reviewable, append-only record.
# Either side changing without the other is a gate failure.
#
# Usage: scripts/check_standards.sh [<biome-binary>]
#   With no argument, the newest built biome under build/{debug,release}/bin is used.
set -euo pipefail
cd "$(dirname "$0")/.."

BIOME="${1:-}"
if [ -z "$BIOME" ]; then
    for c in build/debug/bin/biome build/release/bin/biome; do
        [ -x "$c" ] && BIOME="$c" && break
    done
fi
[ -n "$BIOME" ] && [ -x "$BIOME" ] || { echo "[standards] no built biome binary found (build first, or pass one)"; exit 1; }

shopt -s nullglob
files=(standards/biome-standard-*.toml)
[ ${#files[@]} -gt 0 ] || { echo "[standards] no standards/biome-standard-*.toml files exist"; exit 1; }

fail=0
for f in "${files[@]}"; do
    ver="${f#standards/biome-standard-}"; ver="${ver%.toml}"
    if ! diff -u "$f" <("$BIOME" standards --emit-toml "$ver") >/tmp/standards_drift.diff 2>&1; then
        echo "[standards] DRIFT: $f does not match \`biome standards --emit-toml $ver\`:"
        cat /tmp/standards_drift.diff
        fail=1
    fi
done

# Every standard in biome's table must also have its committed TOML file.
missing=0
while IFS= read -r ver; do
    [ -f "standards/biome-standard-${ver}.toml" ] || { echo "[standards] MISSING: biome knows ${ver} but standards/biome-standard-${ver}.toml is not committed"; missing=1; }
done < <("$BIOME" standards | sed -n 's/^[* ] \([0-9][^ ]*\)  (.*/\1/p')

[ "$fail" -eq 0 ] && [ "$missing" -eq 0 ] || exit 1
echo "[standards] OK: ${#files[@]} standard(s) match biome's table"
