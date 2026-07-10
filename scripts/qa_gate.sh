#!/usr/bin/env bash
# QA gate for cheatah — the quality-control checks that must pass before a push.
#
# Invoked by the git pre-push hook (.githooks/pre-push) for every push to any
# remote/branch, and runnable by hand. Exits non-zero to BLOCK the push.
#
#   1. Coverage: regenerate the README coverage table from clang source-based
#      coverage; FAIL if it changed, and HARD-FAIL unless line + function coverage
#      are both 100% (so pushed code is always fully unit-tested).
#   2. Documentation coverage: 100% Javadoc on the public stdlib API (hard gate) —
#      every type/function/parameter/return documented (scripts/doc_coverage.sh).
#   2b. VS Code extension (hard gate): regenerate its hover database from the stdlib
#      API (Doxygen XML -> gen-hover-docs.py) and FAIL if it drifted, so the editor
#      extension never ships stale relative to the library.
#   3. Configure (debug for tests, release for benchmarks).
#   4. Build (debug).
#   5. Unit test suite (hard gate) — ctest.
#   6. AddressSanitizer + UBSan: build + run the whole suite under sanitizers
#      (hard gate) — catches memory errors and undefined behavior.
#   7. Valgrind memcheck: run EVERY unit test under Valgrind (hard gate) — asserts
#      100% unit-test coverage with no errors/leaks; a second memory checker
#      (security/run-valgrind.sh).
#   8. Benchmarks: build optimized + run a smoke pass (hard gate that they
#      build & run; perf-regression gating comes once we archive history).
#
# This is intentionally lean for the scaffolding stage. As the language grows we
# layer on more rigor (benchmark regression vs archived history, etc.).
#
# Env:  QA_GATE_SKIP=1            bypass the gate entirely (discouraged)
#       QA_GATE_SKIP_COVERAGE=1   skip only the coverage/README-table/100% stage
#       QA_GATE_SKIP_DOCS=1       skip only the documentation-coverage stage
#       QA_GATE_SKIP_EXTENSION=1  skip only the VS Code extension hover-DB sync check
#       QA_GATE_SKIP_ASAN=1       skip only the sanitizer stage (faster local runs)
#       QA_GATE_SKIP_TSAN=1       skip only the ThreadSanitizer stage (faster local runs)
#       QA_GATE_SKIP_VALGRIND=1   skip only the Valgrind stage (faster local runs)
#       QA_GATE_FULL_CR=1         ALSO run the full per-function compile-run battery
#                                 (~200 tests; off by default — opt in when needed)
#       QA_BENCH_MIN_TIME         benchmark min time per case (default 0.05s)
set -uo pipefail

cd "$(git rev-parse --show-toplevel)"

if [ "${QA_GATE_SKIP:-0}" = "1" ]; then
    printf '\n[qa-gate] QA_GATE_SKIP=1 — skipping the QA gate (NOT recommended).\n'
    exit 0
fi

bold() { printf '\n\033[1m[qa-gate] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[qa-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

MIN_TIME="${QA_BENCH_MIN_TIME:-0.05s}"

# The per-function compile-run battery (~200 tests, each forks the compiler+runtime)
# is OPT-IN: set QA_GATE_FULL_CR=1 to include it. By default the gate runs everything
# else — unit tests, the pipeline tests, and the multi-module system apps — and
# excludes the *CompileRun.* tests (in ctest and under Valgrind) to stay fast.
export QA_GATE_FULL_CR="${QA_GATE_FULL_CR:-0}"
if [ "$QA_GATE_FULL_CR" = "1" ]; then
    CR_EXCLUDE=()
else
    CR_EXCLUDE=(--exclude-regex 'CompileRun')
fi

# 1. Coverage: regenerate the README table; fail if it changed (commit it first) --
if [ "${QA_GATE_SKIP_COVERAGE:-0}" = "1" ]; then
    bold "Skipping coverage stage (QA_GATE_SKIP_COVERAGE=1)."
else
    bold "Measuring coverage + refreshing the README table…"
    bash scripts/coverage.sh update-readme >/tmp/cheatah_coverage.log 2>&1 || { tail -30 /tmp/cheatah_coverage.log; fail "coverage report"; }
    if ! git diff --quiet -- README.md; then
        printf '\n[qa-gate] The README coverage table is out of date. Updated it to:\n\n'
        git --no-pager diff -- README.md | sed -n '/coverage:start/,/coverage:end/p'
        fail "README coverage table changed — 'git add README.md && git commit', then push again"
    fi
    cat /tmp/cheatah_coverage.log
    # Hard gate: line + function coverage must be exactly 100%.
    covnums=$(sed -n 's/.*lines [0-9.]*% (\([0-9]*\)\/\([0-9]*\)), functions [0-9.]*% (\([0-9]*\)\/\([0-9]*\)).*/\1 \2 \3 \4/p' /tmp/cheatah_coverage.log)
    [ -n "$covnums" ] || fail "could not parse the coverage summary (coverage.sh output changed?)"
    read -r lcov_n lcov_d fcov_n fcov_d <<<"$covnums"
    if [ "$lcov_n" != "$lcov_d" ] || [ "$fcov_n" != "$fcov_d" ]; then
        fail "unit-test coverage below 100% — lines $lcov_n/$lcov_d, functions $fcov_n/$fcov_d (find gaps with: scripts/coverage.sh show <file>)"
    fi
    bold "Unit-test coverage: 100% lines ($lcov_n/$lcov_d) + functions ($fcov_n/$fcov_d)."
fi

# 1b. Documentation coverage: 100% Javadoc on the public stdlib API (hard gate) --
if [ "${QA_GATE_SKIP_DOCS:-0}" = "1" ]; then
    bold "Skipping documentation-coverage stage (QA_GATE_SKIP_DOCS=1)."
else
    bold "Checking documentation coverage (100% Javadoc)…"
    bash scripts/doc_coverage.sh || fail "documentation coverage below 100% — document the entities listed above"
fi

# 1c. VS Code extension: its hover database (editors/vscode/data/functions.json) is
#     GENERATED from the stdlib API (Doxygen XML -> gen-hover-docs.py), so it must be
#     regenerated and committed whenever the library changes. Regenerate it and FAIL
#     if it drifted, so the editor extension is never shipped stale vs. the library.
if [ "${QA_GATE_SKIP_EXTENSION:-0}" = "1" ]; then
    bold "Skipping VS Code extension check (QA_GATE_SKIP_EXTENSION=1)."
else
    bold "Checking the VS Code extension hover DB is in sync with the stdlib API…"
    DOXYGEN="${DOXYGEN:-doxygen}"
    command -v "$DOXYGEN" >/dev/null 2>&1 || DOXYGEN="$HOME/Tools/doxygen-1.16.1/bin/doxygen"
    command -v "$DOXYGEN" >/dev/null 2>&1 || fail "doxygen not found (needed to regenerate the extension hover DB)"
    rm -rf docs/xml   # Doxygen never prunes; a stale ghost namespace (e.g. a folded module) would drift the hover DB
    "$DOXYGEN" Doxyfile >/tmp/cheatah_ext_doxygen.log 2>&1 || { tail -20 /tmp/cheatah_ext_doxygen.log; fail "doxygen (XML) for the extension check"; }
    python3 editors/vscode/scripts/gen-hover-docs.py >/tmp/cheatah_ext_gen.log 2>&1 || { tail -20 /tmp/cheatah_ext_gen.log; fail "gen-hover-docs.py (extension hover DB)"; }
    # --porcelain catches BOTH a modified and a never-committed (untracked) hover DB.
    if [ -n "$(git status --porcelain -- editors/vscode/data/functions.json)" ]; then
        printf '\n[qa-gate] The VS Code extension hover database is out of date / uncommitted:\n\n'
        git --no-pager diff --stat -- editors/vscode/data/functions.json
        fail "VS Code extension hover DB changed — run 'bash docs/build-docs.sh && python3 editors/vscode/scripts/gen-hover-docs.py', commit editors/vscode/data/functions.json, then push again"
    fi
    bold "VS Code extension hover DB is up to date."
    # The extension's provider unit tests (hover/definition over a fixture user package):
    # headless node, no VS Code needed — see editors/vscode/test/run.js.
    if command -v node >/dev/null 2>&1; then
        bold "Running the VS Code extension provider tests…"
        node editors/vscode/test/run.js || fail "VS Code extension provider tests"
    else
        bold "node not found — skipping the extension provider tests."
    fi
fi

# 2. Configure ---------------------------------------------------------------
bold "Configuring (debug + release)…"
cmake --preset debug   >/tmp/cheatah_cfg_debug.log   2>&1 || { tail -20 /tmp/cheatah_cfg_debug.log;   fail "configure (debug)"; }
cmake --preset release >/tmp/cheatah_cfg_release.log 2>&1 || { tail -20 /tmp/cheatah_cfg_release.log; fail "configure (release)"; }

# 3. Build (debug) -----------------------------------------------------------
bold "Building (debug)…"
cmake --build --preset debug >/tmp/cheatah_build_debug.log 2>&1 || { tail -30 /tmp/cheatah_build_debug.log; fail "debug build"; }

# The websocket system tests (WebSocketSys.*) run cheatah's client against a real Node `ws` echo
# server — the same "real external peer" model as openssl s_server for tls. Install the pinned
# `ws` package (git-ignored) if it isn't already present, so those tests can run.
if [ ! -d tests/fixtures/node_modules/ws ]; then
    if command -v npm >/dev/null 2>&1; then
        bold "Installing the Node 'ws' test peer (tests/fixtures)…"
        ( cd tests/fixtures && (npm ci >/dev/null 2>&1 || npm install >/dev/null 2>&1) ) \
            || fail "npm install of the 'ws' websocket test peer failed (needed for WebSocketSys.*)"
    else
        fail "npm not found — the websocket system tests need the Node 'ws' peer (npm ci in tests/fixtures)"
    fi
fi

# 3b. Pure-cheatah stdlib modules: the build regenerated each one's committed header from its
#     `.purr` via `purrc --emit-library`. The header (and its signed checksum) are committed so
#     the true C++ stays visible and `import` can verify it — fail if they drifted from source.
#     `requests` is currently the only pure-cheatah (.purr) module; `parsers` is hand-written C++.
bold "Checking generated cheatah-module headers are in sync with their .purr sources…"
_cmod_drift="$(git status --porcelain -- 'stdlib/requests/requests.hpp' 'stdlib/requests/requests.hpp.sha512')"
if [ -n "$_cmod_drift" ]; then
    printf '\n[qa-gate] a cheatah module header is out of date / uncommitted:\n\n'
    git --no-pager diff -- stdlib/requests/requests.hpp | head -40
    fail "stdlib/requests/requests.hpp drifted from requests.purr — rebuild, commit the regenerated header (+ .sha512), then push again"
fi

# 4. Unit tests (hard gate) --------------------------------------------------
# The "previously_broken" regression suite (bugs that ONCE broke the toolchain) runs
# FIRST and on its own, so a reintroduced regression fails the gate fast — before the
# full suite. The main run then EXCLUDES that label so they aren't run twice.
bold "Running previously-broken regression suite FIRST…"
ctest --preset debug --output-on-failure -L previously_broken || fail "previously-broken regression suite"

bold "Running unit test suite…"
ctest --preset debug --output-on-failure -LE previously_broken "${CR_EXCLUDE[@]}" || fail "unit tests"

# 5. Sanitizers: build + run the suite under ASan + UBSan (hard gate) ---------
if [ "${QA_GATE_SKIP_ASAN:-0}" = "1" ]; then
    bold "Skipping sanitizer stage (QA_GATE_SKIP_ASAN=1)."
else
    bold "Configuring + building (ASan + UBSan)…"
    cmake --preset asan        >/tmp/cheatah_cfg_asan.log   2>&1 || { tail -20 /tmp/cheatah_cfg_asan.log;   fail "configure (asan)"; }
    cmake --build --preset asan >/tmp/cheatah_build_asan.log 2>&1 || { tail -30 /tmp/cheatah_build_asan.log; fail "asan build"; }
    bold "Running unit test suite under ASan + UBSan…"
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" \
        ctest --preset asan --output-on-failure "${CR_EXCLUDE[@]}" || fail "sanitizer (ASan/UBSan) tests"
fi

# 5b. ThreadSanitizer: build + run the concurrency-relevant suites (hard gate) -
# TSan cannot combine with ASan, so it is its own preset/build. Scoped to the suites that
# actually run threads (thread module, per-thread random, memory once its engine lands) —
# single-threaded suites add no TSan signal, and the purrc subprocess tests spawn
# uninstrumented child binaries TSan cannot see into.
if [ "${QA_GATE_SKIP_TSAN:-0}" = "1" ]; then
    bold "Skipping ThreadSanitizer stage (QA_GATE_SKIP_TSAN=1)."
else
    bold "Configuring + building (TSan)…"
    cmake --preset tsan        >/tmp/cheatah_cfg_tsan.log   2>&1 || { tail -20 /tmp/cheatah_cfg_tsan.log;   fail "configure (tsan)"; }
    cmake --build --preset tsan --target cheatah_tests >/tmp/cheatah_build_tsan.log 2>&1 || { tail -30 /tmp/cheatah_build_tsan.log; fail "tsan build"; }
    bold "Running concurrency-relevant suites under ThreadSanitizer…"
    TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" \
        ctest --preset tsan --output-on-failure -R 'CheatahThread|CheatahRandom|Memory' \
        || fail "ThreadSanitizer tests"
fi

# 6. Valgrind memcheck: run the unit tests under Valgrind (hard gate) ---------
if [ "${QA_GATE_SKIP_VALGRIND:-0}" = "1" ]; then
    bold "Skipping Valgrind stage (QA_GATE_SKIP_VALGRIND=1)."
elif ! command -v valgrind >/dev/null 2>&1; then
    fail "valgrind not installed (install it, or set QA_GATE_SKIP_VALGRIND=1)"
else
    bold "Running unit tests under Valgrind memcheck…"
    bash security/run-valgrind.sh >/tmp/cheatah_valgrind.log 2>&1 || { tail -50 /tmp/cheatah_valgrind.log; fail "valgrind memcheck"; }
fi

# 7. Benchmarks: build optimized + smoke run (hard gate) ---------------------
bold "Building benchmarks (release)…"
cmake --build --preset release-benchmarks >/tmp/cheatah_build_bench.log 2>&1 || { tail -30 /tmp/cheatah_build_bench.log; fail "release benchmark build"; }

bold "Running benchmarks (smoke pass, min-time ${MIN_TIME})…"
./build/release/bin/cheatah_benchmarks --benchmark_min_time="${MIN_TIME}" || fail "benchmark run"

# 7b. Fixed-vs-GLM performance gate (hard gate) ------------------------------
#     linalg::Fixed exists to match GLM at GLM's own game; assert it still does, so a change that
#     quietly de-vectorizes a hot path fails here rather than in a consumer's frame budget. Tolerant
#     by ratio AND absolute gap, with a confirmation re-run, so sub-nanosecond noise never flakes it.
bold "Performance gate: linalg::Fixed vs GLM…"
./scripts/bench_gate.sh || fail "linalg::Fixed regressed against GLM"

# 8. Refresh the editor (best-effort, NOT a gate) ----------------------------
#    The runtime was just rebuilt above; package the VS Code extension with the
#    current hover DB + a copy of THIS runtime's stdlib headers, and (re)install it,
#    so the editor never drifts from the built runtime. Never fails the gate: it
#    no-ops if `code`/vsce are unavailable (headless CI), and errors are swallowed.
if [ "${QA_GATE_SKIP_EDITOR:-0}" = "1" ]; then
    bold "Skipping editor refresh (QA_GATE_SKIP_EDITOR=1)."
else
    bold "Refreshing the VS Code extension from the freshly-built runtime…"
    bash editors/vscode/scripts/install-extension.sh || \
        printf '[qa-gate] editor refresh skipped/failed (non-fatal).\n'
fi

# 8b. Static analysis: cppcheck for performance + security problems ----------
bold "Running cppcheck (performance + security)…"
bash scripts/cppcheck.sh || fail "cppcheck (performance/security findings)"

# 9. Stage the release for review (only on a release commit) -----------------
#    review/ then holds a copy of the latest release to inspect before pushing; it is
#    archived (per version) when the release tag is actually pushed — see the pre-push
#    hook. Best-effort — never fails the gate.
if git log -1 --format=%s | grep -qE '^release: '; then
    bold "Staging the release into review/ (release commit)…"
    bash scripts/stage_review.sh || printf '[qa-gate] review staging skipped/failed (non-fatal).\n'
fi

bold "QA gate PASSED — push may proceed."
exit 0
