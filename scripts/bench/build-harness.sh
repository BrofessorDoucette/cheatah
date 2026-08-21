#!/usr/bin/env bash
# build-harness.sh — compile a .purr benchmark harness that imports the shared `stamp` module.
#
# purrc resolves `import stamp` to an emitted LIBRARY HEADER, not to a .purr on disk, so the
# module has to be emitted once before any harness that imports it will compile. That is the
# same two-step docs/build-docs.sh performs for the biome module; this script exists so the
# three benchmark harnesses do not each reinvent it.
#
#   bash scripts/bench/build-harness.sh <harness.purr> <out.so>
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

SRC="${1:?usage: build-harness.sh <harness.purr> <out.so>}"
OUT="${2:?usage: build-harness.sh <harness.purr> <out.so>}"

PURRC=build/release/bin/purrc
[ -x "$PURRC" ] || { echo "build-harness: missing $PURRC — build the release preset first"; exit 1; }

LIBDIR=build/bench-lib
STAMP_HPP="$LIBDIR/stamp.hpp"
if [ ! -f "$STAMP_HPP" ] || [ scripts/bench/stamp.purr -nt "$STAMP_HPP" ] || [ "$PURRC" -nt "$STAMP_HPP" ]; then
    mkdir -p "$LIBDIR"
    # --transparent: header-only, so a harness links nothing extra. Library mode also drops a
    # top-level entry call, which stamp.purr does not have anyway.
    "$PURRC" --emit-library --transparent scripts/bench/stamp.purr -o "$STAMP_HPP"
fi

"$PURRC" "$SRC" -o "$OUT" --import-root "$(pwd)/$LIBDIR"
