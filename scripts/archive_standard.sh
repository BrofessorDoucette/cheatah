#!/usr/bin/env bash
# Archive a Biome Standard: archive/biome-standard-<ver>/ gets the standard's canonical
# TOML plus a source tarball of EVERY member at its pinned tag (git-archive of the tag,
# taken from the sibling checkout). This is the on-disk half of the source-retention
# guarantee: even decades later, the exact tested-together sources of a standard are one
# directory. Run after the member tags exist (i.e. after the release wave that defines
# the standard). archive/ is git-ignored, like the per-release tarballs.
#
# Usage: scripts/archive_standard.sh <standard-version>       e.g. 0.1.0-alpha
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

VER="${1:-}"
[ -n "$VER" ] || { echo "usage: archive_standard.sh <standard-version>"; exit 1; }
TOML="standards/biome-standard-${VER}.toml"
[ -f "$TOML" ] || { echo "[standard-archive] no ${TOML}"; exit 1; }

DEST="archive/biome-standard-${VER}"
mkdir -p "$DEST"
cp "$TOML" "$DEST/"

# Member repos live as siblings of this checkout; cheatah is this repo itself.
fail=0
while IFS='=' read -r name tag; do
    name="$(echo "$name" | tr -d ' ')"
    tag="$(echo "$tag" | tr -d ' "')"
    [ -n "$name" ] && [ -n "$tag" ] || continue
    if [ "$name" = "cheatah" ]; then repo="."; else repo="../$name"; fi
    out="$DEST/${name}-${tag}.tar.gz"
    if [ ! -d "$repo/.git" ] && ! git -C "$repo" rev-parse --git-dir >/dev/null 2>&1; then
        echo "[standard-archive] MISSING repo for member ${name} (expected at ${repo})"; fail=1; continue
    fi
    if ! git -C "$repo" rev-parse -q --verify "refs/tags/${tag}" >/dev/null; then
        echo "[standard-archive] MISSING tag ${tag} in ${name} — cut the member release first"; fail=1; continue
    fi
    git -C "$repo" archive --format=tar.gz --prefix="${name}-${tag}/" -o "$(pwd)/$out" "$tag"
    echo "[standard-archive] $out  ($(du -h "$out" | cut -f1))"
done < <(sed -n '/^\[components\]/,$p' "$TOML" | grep '=')

[ "$fail" -eq 0 ] || { echo "[standard-archive] incomplete — fix the missing members and re-run"; exit 1; }
echo "[standard-archive] Biome Standard ${VER} archived in ${DEST}/"
