#!/usr/bin/env bash
# Compile every gen/*.purr with the freshly-built purrc and drop the generated C++ next
# to each source as <name>.gen.cpp, so you can see exactly what the transpiler emits.
#
# Run it directly, or via the VS Code task "gen: regenerate .gen.cpp".
#
# Building the `purrc` target also (re)builds the stdlib static libraries it links, so
# this always uses the LATEST compiler + library. The compiled module artifacts go to
# gen/.out/ (git-ignored); only the .purr sources and their .gen.cpp are kept.
set -euo pipefail
cd "$(dirname "$0")/.."                     # repo root

PRESET="${CHEATAH_PRESET:-release}"
BUILD="build/${PRESET}"
PURRC="${BUILD}/bin/purrc"

echo "[gen] building latest purrc (${PRESET})…"
cmake --build "${BUILD}" --target purrc >/dev/null

mkdir -p gen/.out
shopt -s nullglob
count=0
for f in gen/*.purr; do
    name="$(basename "$f" .purr)"
    "${PURRC}" "$f" -o "gen/.out/${name}.so" >/dev/null
    mv -f "gen/.out/${name}.so.gen.cpp" "gen/${name}.gen.cpp"
    echo "[gen] ${f}  ->  gen/${name}.gen.cpp"
    count=$((count + 1))
done
echo "[gen] done — ${count} program(s)."
