#!/usr/bin/env bash
# Stage a copy of the latest RELEASE build into review/ so it can be inspected before the
# release is pushed. Called by the QA gate after a passing run on a release commit, and
# runnable directly or via the "review: stage latest release" VS Code task.
#
# review/ holds ONLY the latest version (it is rebuilt each time) and is git-ignored.
# This is a QA convenience step — Linux-only, no cross-platform concern.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

# Canonical release version = the newest heading in CHANGELOG.md (e.g. "v1.1.0-alpha").
VERSION="$(grep -m1 -oE '^## v[0-9][^ ]*' CHANGELOG.md | awk '{print $2}')"
[ -n "${VERSION:-}" ] || { echo "[review] could not read a version from CHANGELOG.md"; exit 1; }
NAME="cheatah-${VERSION}"
DEST="review/${NAME}"
REL="build/release"

echo "[review] building the release toolchain for ${VERSION}…"
cmake --preset release >/dev/null 2>&1 || true
cmake --build --preset release --target purrc cheatah cheatah_stdlib >/dev/null 2>&1 \
    || { echo "[review] release build failed"; exit 1; }

# Assemble a self-contained toolchain bundle: the binaries, the static stdlib archives
# purrc links into programs, the public module headers programs compile against, and the
# top-level metadata.
rm -rf review
mkdir -p "${DEST}/bin" "${DEST}/lib" "${DEST}/include"
cp "${REL}/bin/purrc" "${REL}/bin/cheatah" "${DEST}/bin/"
cp ${REL}/lib/libcheatah_*.a "${DEST}/lib/"
while IFS= read -r h; do
    rel="${h#stdlib/}"
    mkdir -p "${DEST}/include/$(dirname "$rel")"
    cp "$h" "${DEST}/include/${rel}"
done < <(git ls-files 'stdlib/*/*.hpp' | grep -v '/tests/')
cp CHANGELOG.md README.md LICENSE "${DEST}/" 2>/dev/null || true
printf '%s\n' "$VERSION" >"${DEST}/VERSION"

echo "[review] staged ${DEST}  ($(du -sh "${DEST}" | cut -f1))"
