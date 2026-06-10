#!/usr/bin/env bash
# Static analysis: run cppcheck across the repo's C++ for PERFORMANCE and SECURITY
# problems. Part of the QA gate; also runnable on its own:
#
#     bash scripts/cppcheck.sh
#
# What it enables:
#   - error severity (always on)  — buffer overruns, null/uninit derefs, leaks, …
#   - warning                     — likely-bug security issues (uninitialised data, …)
#   - performance                 — pass-by-value, redundant copies, postfix++ on objects
#   - portability                 — implementation-defined / non-portable constructs
#
# What it SUPPRESSES (the spammy things cppcheck likes to do):
#   - missingInclude / missingIncludeSystem  — we don't feed it the full include graph
#   - unusedFunction                         — library APIs look "unused" from here
#   - checkersReport / information noise
# A real finding exits non-zero (so the gate fails); `// cppcheck-suppress <id>` inline
# can annotate an intentional exception.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

command -v cppcheck >/dev/null 2>&1 || {
    echo "cppcheck not found — install it (e.g. 'sudo apt install cppcheck')."
    exit 1
}

cppcheck \
    --enable=warning,performance,portability \
    --std=c++20 --language=c++ \
    --inline-suppr \
    --suppress=missingInclude \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=checkersReport \
    --suppress=unmatchedSuppression \
    --error-exitcode=1 \
    -q \
    -j "$(nproc)" \
    -i build -i books -i docs \
    stdlib compiler runtime tests \
    || { echo "[cppcheck] performance/security findings above — fix them or annotate with // cppcheck-suppress"; exit 1; }

echo "[cppcheck] clean — no performance/security findings."
