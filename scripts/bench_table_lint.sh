#!/usr/bin/env bash
# bench_table_lint.sh — compile and run scripts/bench_table.purr.
#
# The lint itself is written in cheatah, not Python: QA tooling is cheatah in this repo (the
# CPython/NumPy *baselines* are a different thing and legitimately stay Python). This wrapper
# exists only because the QA gate speaks shell — it builds the .purr into a temp .so and runs
# it from the repo root, exactly as scripts/docs_a11y_gate.sh does for a11y_check.purr.
#
#   bash scripts/bench_table_lint.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

PURRC=build/release/bin/purrc
CHEATAH=build/release/bin/cheatah

# Skip rather than fail when the release build is absent: this lane also runs before the
# release preset has been configured on a clean checkout, and a missing compiler is a build
# state, not a documentation defect.
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    echo "[bench-table] SKIP — no release build (need $PURRC and $CHEATAH)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$PURRC" scripts/bench_table.purr -o "$TMP/bench_table.so" >/dev/null \
    || { echo "[bench-table] FAILED: purrc scripts/bench_table.purr"; exit 1; }
# Forward arguments so the release lane can pass `check --strict`, where staleness is fatal.
# No arguments means `check`, which warns on staleness instead.
"$CHEATAH" "$TMP/bench_table.so" "$@"
