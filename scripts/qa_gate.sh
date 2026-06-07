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
#       QA_GATE_SKIP_ASAN=1       skip only the sanitizer stage (faster local runs)
#       QA_GATE_SKIP_VALGRIND=1   skip only the Valgrind stage (faster local runs)
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

# 2. Configure ---------------------------------------------------------------
bold "Configuring (debug + release)…"
cmake --preset debug   >/tmp/cheatah_cfg_debug.log   2>&1 || { tail -20 /tmp/cheatah_cfg_debug.log;   fail "configure (debug)"; }
cmake --preset release >/tmp/cheatah_cfg_release.log 2>&1 || { tail -20 /tmp/cheatah_cfg_release.log; fail "configure (release)"; }

# 3. Build (debug) -----------------------------------------------------------
bold "Building (debug)…"
cmake --build --preset debug >/tmp/cheatah_build_debug.log 2>&1 || { tail -30 /tmp/cheatah_build_debug.log; fail "debug build"; }

# 4. Unit tests (hard gate) --------------------------------------------------
bold "Running unit test suite…"
ctest --preset debug --output-on-failure || fail "unit tests"

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
        ctest --preset asan --output-on-failure || fail "sanitizer (ASan/UBSan) tests"
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

bold "QA gate PASSED — push may proceed."
exit 0
