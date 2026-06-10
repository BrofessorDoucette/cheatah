#!/usr/bin/env bash
# Archive the staged review/ bundle into archive/<name>.tar.gz. The archive keeps ONLY
# versions that are ACTUALLY RELEASED — i.e. whose version tag has been pushed to GitHub.
#
# Two ways in:
#   archive_release.sh --pushing <tag>   from the pre-push hook, as the tag is being
#                                        pushed (that push IS the release).
#   archive_release.sh [<version>]       manual / VS Code task — requires the tag to
#                                        already exist on the remote.
#
# QA convenience step — Linux-only, no cross-platform concern. archive/ is git-ignored.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

require_released=1
if [ "${1:-}" = "--pushing" ]; then
    require_released=0          # the tag is being pushed right now — that is the release
    VERSION="${2:-}"
else
    VERSION="${1:-}"
fi
# Default to whatever is staged in review/ when no version is given.
if [ -z "${VERSION:-}" ]; then
    VERSION="$(cat review/cheatah-*/VERSION 2>/dev/null | head -1 || true)"
fi
[ -n "${VERSION:-}" ] || { echo "[archive] nothing staged in review/ and no version given"; exit 0; }

SRC="review/cheatah-${VERSION}"
[ -d "$SRC" ] || { echo "[archive] review/ has no ${VERSION} bundle — stage it first"; exit 0; }

# Keep ONLY released versions: unless we are mid-push of the tag, require it on the remote.
if [ "$require_released" = "1" ]; then
    if ! git ls-remote --tags origin "refs/tags/${VERSION}" 2>/dev/null | grep -q .; then
        echo "[archive] ${VERSION} is not pushed to GitHub — refusing (archive keeps only released versions)"
        exit 0
    fi
fi

mkdir -p archive
TARBALL="archive/cheatah-${VERSION}.tar.gz"
tar -czf "$TARBALL" -C review "cheatah-${VERSION}"
echo "[archive] ${TARBALL}  ($(du -h "$TARBALL" | cut -f1))"
