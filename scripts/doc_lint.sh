#!/usr/bin/env bash
# doc_lint.sh — compile and run scripts/doc_lint.purr (the documentation-truth lint).
#
# Written in cheatah, like the other QA tooling; this wrapper exists only because the QA gate
# and the pre-push hook speak shell. It builds the .purr into a temp .so and runs it from the
# repo root, exactly as scripts/bench_table_lint.sh does.
#
#   bash scripts/doc_lint.sh [tests|links|examples|flags|phrases|all]
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

PURRC=build/release/bin/purrc
CHEATAH=build/release/bin/cheatah
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    PURRC=build/debug/bin/purrc
    CHEATAH=build/debug/bin/cheatah
fi
# Skip rather than fail when no toolchain is built: a missing compiler is a build state, not a
# documentation defect — and the skip is printed, never silent.
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    echo "[doc-lint] SKIP — no purrc/cheatah build (need build/release or build/debug)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$PURRC" scripts/doc_lint.purr -o "$TMP/doc_lint.so" >/dev/null \
    || { echo "[doc-lint] FAILED: purrc scripts/doc_lint.purr"; exit 1; }
"$CHEATAH" "$TMP/doc_lint.so" "${1:-all}"
