#!/usr/bin/env bash
# QA gate for cheatah — the quality-control checks that must pass before a push.
#
# Invoked by the git pre-push hook (.githooks/pre-push) for every push to any
# remote/branch, and runnable by hand. Exits non-zero to BLOCK the push.
#
# WHAT is checked is unchanged from the original sequential gate; only the SCHEDULE
# changed. Stages with no dependency on the C++ build tree run as BACKGROUND LANES
# from the start, overlapping the build/test chain; every lane is joined (exit code
# checked, log tailed on failure) before the gate can declare PASSED. Measured
# benchmarks still run LAST, alone (their tolerance floors assume a quiet machine).
#
#   lanes @ t=0:   coverage (own build/cov tree) · doc-coverage · cppcheck ·
#                  extension hover-DB drift check (1c)
#   foreground:    configure (then bench build in bg) → debug build → module-header
#                  drift → golden-master → previously-broken → unit ctest →
#                  ASan build+tests (TSan build in bg) → TSan tests → Valgrind
#   join barrier:  coverage verdict · docs · extension · cppcheck · bench build
#   tail:          editor refresh (bg, non-fatal) → benchmark smoke (sharded) →
#                  join editor → Fixed-vs-GLM perf gate → release staging → PASS
#
# Stage inventory (numbering preserved from the sequential gate):
#   1. Coverage: regenerate the README coverage table from clang source-based
#      coverage; FAIL if it changed, and HARD-FAIL unless line + function coverage
#      are both 100% (so pushed code is always fully unit-tested).
#   1b. Documentation coverage: 100% Javadoc on the public stdlib API, then the
#       doc-tag lint (@complexity + @alloc + a test link on every public function).
#   1c. VS Code extension (hard gate): regenerate its hover database from the stdlib
#       API (Doxygen XML -> gen-hover-docs.py) and FAIL if it drifted.
#   2. Configure (debug for tests, release for benchmarks).
#   3. Build (debug); 3b module-header drift; 3c frontend golden-master; 3d Biome
#      Standard drift (standards/*.toml must match biome's in-source table).
#   4. Unit test suite (hard gate) — ctest, parallel.
#   5. ASan+UBSan build + suite; 5b TSan build + concurrency suites.
#   6. Valgrind memcheck (sharded) — 100% unit-test coverage, no errors/leaks.
#   7. Benchmarks: smoke pass (sharded; timings discarded) then the Fixed-vs-GLM
#      performance gate (scripts/bench_gate.sh — the measured authority).
#   8. Editor refresh (best-effort) · 8b cppcheck + private-reference scan · 9 release
#      staging.
#
# Env:  QA_GATE_SKIP=1            bypass the gate entirely (discouraged)
#       QA_GATE_SKIP_COVERAGE=1   skip only the coverage/README-table/100% stage
#       QA_GATE_SKIP_DOCS=1       skip only the documentation-coverage stage
#       QA_GATE_SKIP_EXTENSION=1  skip only the VS Code extension hover-DB sync check
#       QA_GATE_SKIP_ASAN=1       skip only the sanitizer stage (faster local runs)
#       QA_GATE_SKIP_TSAN=1       skip only the ThreadSanitizer stage (faster local runs)
#       QA_GATE_SKIP_VALGRIND=1   skip only the Valgrind stage (faster local runs)
#       QA_GATE_SKIP_EDITOR=1     skip only the (non-fatal) editor refresh
#       QA_GATE_FULL_CR=1         ALSO run the full per-function compile-run battery
#                                 (~200 tests; off by default — opt in when needed)
#       QA_GATE_JOBS              ctest parallelism (default nproc; =1 to serialize)
#       QA_BENCH_MIN_TIME         benchmark min time per case (default 0.05s)
#       BENCH_SMOKE_JOBS          smoke-pass shards (default nproc; =1 = serial)
#       BENCH_GATE_JOBS           bench_gate pass-1 shards (default 8; =1 = serial)
#       COV_JOBS                  coverage unit-run shards (default nproc; =1 = serial)
set -uo pipefail

cd "$(git rev-parse --show-toplevel)"

if [ "${QA_GATE_SKIP:-0}" = "1" ]; then
    printf '\n[qa-gate] QA_GATE_SKIP=1 — skipping the QA gate (NOT recommended).\n'
    exit 0
fi

bold() { printf '\n\033[1m[qa-gate] %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31m[qa-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

MIN_TIME="${QA_BENCH_MIN_TIME:-0.05s}"

# Run ctest in parallel. The unit/e2e tests are independent processes (temp files are
# per-test-name; sockets bind to OS-assigned ports), so they parallelize with zero
# rigor change — SAME tests, SAME -O3 -march=native compiles, SAME binaries, just
# concurrent. On this project's suite (~750 tests) this is ~9x wall-clock. Override
# with QA_GATE_JOBS (e.g. =1 to serialize when debugging a flaky interaction).
JOBS="${QA_GATE_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"

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

# ---- background-lane harness -----------------------------------------------------
# One fixed variable pair per lane (PID_x/LOG_x) — plain variables, not associative
# arrays, because macOS stock bash is 3.2. A lane's stdout/stderr goes to its log;
# bg_join checks its exit code and, on failure, tails that log and fails the gate —
# the same failure surface each stage had when it ran inline. bg_poll gives near-
# fail-fast: between foreground stages, any lane that has already EXITED is joined
# immediately, so its failure surfaces within seconds instead of at the barrier.
# The EXIT trap kills still-running lanes when the gate dies early, so a failed run
# never leaves a stray coverage.sh mid-rewrite of README.md (or an orphan doxygen).
PID_COV=""; PID_COVFIN=""; PID_DOCS=""; PID_EXT=""; PID_CPPCHECK=""; PID_BENCHBUILD=""; PID_TSANBUILD=""; PID_EDITOR=""
LOG_COV=/tmp/cheatah_coverage.log
LOG_COVFIN=/tmp/cheatah_coverage_finish.log
LOG_DOCS=/tmp/cheatah_docs.log
LOG_EXT=/tmp/cheatah_ext_check.log
LOG_CPPCHECK=/tmp/cheatah_cppcheck.log
LOG_BENCHBUILD=/tmp/cheatah_build_bench.log
LOG_TSANBUILD=/tmp/cheatah_build_tsan.log
LOG_EDITOR=/tmp/cheatah_editor.log

bg_join() {  # bg_join <pid-var-name> <log> <label>  — blocking; fail (with log tail) on nonzero
    local _var="$1" _log="$2" _label="$3" _pid
    eval "_pid=\"\$$_var\""
    [ -n "$_pid" ] || return 0
    eval "$_var=''"
    wait "$_pid" || { tail -40 "$_log"; fail "$_label"; }
}

bg_poll() {  # reap any lane that has already exited (fail fast); running lanes are left alone
    local _v _l _t _pid
    for _spec in \
        "PID_COV:$LOG_COV:coverage report" \
        "PID_DOCS:$LOG_DOCS:documentation coverage or contract tags incomplete — see the entities listed above" \
        "PID_EXT:$LOG_EXT:VS Code extension hover-DB check" \
        "PID_CPPCHECK:$LOG_CPPCHECK:cppcheck findings, or a private-project reference in the public tree" \
        "PID_BENCHBUILD:$LOG_BENCHBUILD:release benchmark build" \
        "PID_TSANBUILD:$LOG_TSANBUILD:tsan build"; do
        _v="${_spec%%:*}"; _t="${_spec##*:}"; _l="${_spec#*:}"; _l="${_l%:*}"
        eval "_pid=\"\$$_v\""
        [ -n "$_pid" ] || continue
        kill -0 "$_pid" 2>/dev/null && continue   # still running — not our concern yet
        bg_join "$_v" "$_l" "$_t"
    done
}

cleanup() {
    local _pid
    for _pid in "$PID_COV" "$PID_COVFIN" "$PID_DOCS" "$PID_EXT" "$PID_CPPCHECK" "$PID_BENCHBUILD" "$PID_TSANBUILD" "$PID_EDITOR"; do
        [ -n "$_pid" ] || continue
        kill -- "-$_pid" 2>/dev/null || kill "$_pid" 2>/dev/null
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT

# Launch a lane in its own process group (so cleanup can kill the whole tree; setsid
# is Linux — macOS falls back to a plain background job whose direct child we can kill).
bg_launch() {  # bg_launch <pid-var-name> <log> <cmd…>
    local _var="$1" _log="$2"; shift 2
    : >"$_log"
    if command -v setsid >/dev/null 2>&1; then
        setsid "$@" >"$_log" 2>&1 &
    else
        "$@" >"$_log" 2>&1 &
    fi
    eval "$_var=$!"
}

# ---- hoisted prerequisite: the Node 'ws' websocket test peer ----------------------
# Both this gate's ctest suites AND coverage.sh (a background lane below) install it
# when missing; install it ONCE, up front, so two installers never race the same
# tests/fixtures/node_modules directory.
if [ ! -d tests/fixtures/node_modules/ws ]; then
    if command -v npm >/dev/null 2>&1; then
        bold "Installing the Node 'ws' test peer (tests/fixtures)…"
        ( cd tests/fixtures && (npm ci >/dev/null 2>&1 || npm install >/dev/null 2>&1) ) \
            || fail "npm install of the 'ws' websocket test peer failed (needed for WebSocketSys.*)"
    else
        fail "npm not found — the websocket system tests need the Node 'ws' peer (npm ci in tests/fixtures)"
    fi
fi

# Lane git calls must never contend with the foreground's for the index lock.
export GIT_OPTIONAL_LOCKS=0

# ---- stage 1c body (as a function, for the extension lane) ------------------------
# Verbatim behavior: regenerate docs/xml + the hover DB, fail on drift, then run the
# extension's headless provider tests. Its bold/fail output lands in the lane log.
lane_extension_check() {
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
}

# ---- launch the no-build-dependency lanes (t = 0) ---------------------------------
# 1. Coverage PREPARE: its OWN tree (build/cov), instrumented build + the sharded UNIT
#    run — no fixed network ports, safe to overlap anything. The TlsSys/WebSocketSys
#    system loop (fixed ports 479xx + global pkill teardown) is the FINISH phase,
#    launched only after Valgrind — the foreground's last TlsSys consumer — so two
#    processes never fight over the same s_server port (that exact collision failed
#    a TlsSys handshake when the whole stage ran as one overlapped lane).
if [ "${QA_GATE_SKIP_COVERAGE:-0}" = "1" ]; then
    bold "Skipping coverage stage (QA_GATE_SKIP_COVERAGE=1)."
else
    bold "Coverage: instrumented build + sharded unit run (background lane)…"
    bg_launch PID_COV "$LOG_COV" bash scripts/coverage.sh --phase=prepare
fi
# 1b. Documentation coverage (single strict doxygen parse; writes nothing), then the
#     doc-TAG lint in the same lane: every public stdlib function must carry
#     @complexity, @alloc, and a @test/@crtest/@systest link (scripts/doc_tag_lint.sh).
if [ "${QA_GATE_SKIP_DOCS:-0}" = "1" ]; then
    bold "Skipping documentation-coverage stage (QA_GATE_SKIP_DOCS=1)."
else
    bold "Checking documentation coverage + contract tags (background lane)…"
    bg_launch PID_DOCS "$LOG_DOCS" bash -c 'bash scripts/doc_coverage.sh && bash scripts/doc_tag_lint.sh'
fi
# 1c. Extension hover-DB drift check (its own doxygen run + node tests).
if [ "${QA_GATE_SKIP_EXTENSION:-0}" = "1" ]; then
    bold "Skipping VS Code extension check (QA_GATE_SKIP_EXTENSION=1)."
else
    bold "Checking the VS Code extension hover DB (background lane)…"
    bg_launch PID_EXT "$LOG_EXT" bash -c '
        set -uo pipefail
        cd "$(git rev-parse --show-toplevel)"
        bold() { printf "\n\033[1m[qa-gate] %s\033[0m\n" "$*"; }
        fail() { printf "\n\033[31m[qa-gate] FAILED: %s\033[0m\n" "$*"; exit 1; }
        '"$(declare -f lane_extension_check)"'
        lane_extension_check'
fi
# 8b. cppcheck (pure source analysis, already -j nproc internally) + the private-reference
#     scan (sibling-project names must not reach the public tree — see
#     scripts/check_no_private_refs.sh; the commit-msg / pre-push hooks cover messages).
bold "Running cppcheck + private-reference scan (background lane)…"
bg_launch PID_CPPCHECK "$LOG_CPPCHECK" bash -c 'bash scripts/cppcheck.sh && bash scripts/check_no_private_refs.sh'

# 2. Configure ---------------------------------------------------------------
bold "Configuring (debug + release)…"
cmake --preset debug   >/tmp/cheatah_cfg_debug.log   2>&1 || { tail -20 /tmp/cheatah_cfg_debug.log;   fail "configure (debug)"; }
cmake --preset release >/tmp/cheatah_cfg_release.log 2>&1 || { tail -20 /tmp/cheatah_cfg_release.log; fail "configure (release)"; }

# 7-prep. Benchmark build overlaps the whole debug/test chain (needed only at stage 7).
bg_launch PID_BENCHBUILD "$LOG_BENCHBUILD" cmake --build --preset release-benchmarks

# 3. Build (debug) -----------------------------------------------------------
bold "Building (debug)…"
cmake --build --preset debug >/tmp/cheatah_build_debug.log 2>&1 || { tail -30 /tmp/cheatah_build_debug.log; fail "debug build"; }

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

# 3c. Frontend golden-master (hard gate) — runs FIRST and fork-free (~20 ms). The
#     lexer->parser->codegen output must stay byte-identical for a fixed .purr corpus;
#     a compiler-speed refactor that alters emitted C++ is a behavior change and fails
#     here immediately, before the slow suites. Re-capture (reviewed) with
#     CHEATAH_GOLDEN_UPDATE=1 ctest --preset debug -R golden-master.
bold "Checking frontend golden-master (emitted C++ is byte-identical)…"
ctest --preset debug --output-on-failure --parallel "$JOBS" -R 'golden-master' || fail "frontend golden-master drifted — the transpiler changed its emitted C++"
bg_poll

# 3d. Biome Standard drift (hard gate) — the append-only standards/*.toml files must be
#     byte-identical to biome's in-source table (each side is meaningless without the other).
bold "Checking Biome Standard files match biome's in-source table…"
bash scripts/check_standards.sh build/debug/bin/biome || fail "Biome Standard drift — standards/*.toml and biome.purr's known_standards() disagree"

# 4. Unit tests (hard gate) --------------------------------------------------
# The "previously_broken" regression suite (bugs that ONCE broke the toolchain) runs
# FIRST and on its own, so a reintroduced regression fails the gate fast — before the
# full suite. The main run then EXCLUDES that label so they aren't run twice.
bold "Running previously-broken regression suite FIRST…"
ctest --preset debug --output-on-failure --parallel "$JOBS" -L previously_broken || fail "previously-broken regression suite"
bg_poll

bold "Running unit test suite…"
ctest --preset debug --output-on-failure --parallel "$JOBS" -LE previously_broken "${CR_EXCLUDE[@]}" || fail "unit tests"
bg_poll

# 5b-prep. TSan configure+build overlaps the ASan stage (it is needed only at 5b).
if [ "${QA_GATE_SKIP_TSAN:-0}" != "1" ]; then
    bg_launch PID_TSANBUILD "$LOG_TSANBUILD" bash -c \
        'cmake --preset tsan 2>&1 && cmake --build --preset tsan --target cheatah_tests'
fi

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
        ctest --preset asan --output-on-failure --parallel "$JOBS" "${CR_EXCLUDE[@]}" || fail "sanitizer (ASan/UBSan) tests"
fi
bg_poll

# 5b. ThreadSanitizer: run the concurrency-relevant suites (hard gate) --------
# TSan cannot combine with ASan, so it is its own preset/build (prepared in the
# background above). Scoped to the suites that actually run threads — single-threaded
# suites add no TSan signal, and the purrc subprocess tests spawn uninstrumented
# child binaries TSan cannot see into. The TSan TEST RUN never overlaps the ASan or
# Valgrind runs (only builds overlap): concurrency suites are timing-sensitive.
if [ "${QA_GATE_SKIP_TSAN:-0}" = "1" ]; then
    bold "Skipping ThreadSanitizer stage (QA_GATE_SKIP_TSAN=1)."
else
    bg_join PID_TSANBUILD "$LOG_TSANBUILD" "tsan build"
    bold "Running concurrency-relevant suites under ThreadSanitizer…"
    TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" \
        ctest --preset tsan --output-on-failure --parallel "$JOBS" -R 'CheatahThread|CheatahRandom|Memory' \
        || fail "ThreadSanitizer tests"
fi
bg_poll

# 6. Valgrind memcheck: run the unit tests under Valgrind (hard gate) ---------
#    Valgrind is unavailable/broken on macOS (especially Apple Silicon); the memory-safety
#    coverage there is the ASan+UBSan stage above, so skip Valgrind on Darwin rather than fail.
if [ "${QA_GATE_SKIP_VALGRIND:-0}" = "1" ]; then
    bold "Skipping Valgrind stage (QA_GATE_SKIP_VALGRIND=1)."
elif [ "$(uname -s)" = "Darwin" ]; then
    bold "Skipping Valgrind stage on macOS (unsupported on Apple Silicon; ASan+UBSan covered it above)."
elif ! command -v valgrind >/dev/null 2>&1; then
    fail "valgrind not installed (install it, or set QA_GATE_SKIP_VALGRIND=1)"
else
    bold "Running unit tests under Valgrind memcheck…"
    bash security/run-valgrind.sh >/tmp/cheatah_valgrind.log 2>&1 || { tail -50 /tmp/cheatah_valgrind.log; fail "valgrind memcheck"; }
fi
bg_poll

# ---- join barrier: every background lane must be green ----------------------------
# 1. Coverage: join the PREPARE lane, then start the FINISH phase (the fixed-port
#    TlsSys/WebSocketSys loop + profile merge + README rewrite). Valgrind — the last
#    foreground TlsSys consumer — is done, and everything still running from here on
#    (barrier joins, editor refresh, the sharded smoke) uses no TLS fixture ports.
#    Its verdict is checked right before the measured perf gate below.
if [ "${QA_GATE_SKIP_COVERAGE:-0}" != "1" ]; then
    bg_join PID_COV "$LOG_COV" "coverage report (instrumented build + unit run)"
    bold "Coverage: system-test loop + report (background lane)…"
    bg_launch PID_COVFIN "$LOG_COVFIN" bash scripts/coverage.sh --phase=finish update-readme
fi
# 1b/1c/8b/7-prep joins (each prints its lane log tail + fails the gate on nonzero).
bg_join PID_DOCS "$LOG_DOCS" "documentation coverage or contract tags incomplete — see the entities listed above"
[ -z "$PID_DOCS" ] && [ "${QA_GATE_SKIP_DOCS:-0}" != "1" ] && bold "Documentation coverage lane: OK."
bg_join PID_EXT "$LOG_EXT" "VS Code extension hover-DB check"
[ "${QA_GATE_SKIP_EXTENSION:-0}" != "1" ] && bold "VS Code extension lane: OK (details in $LOG_EXT)."
bg_join PID_CPPCHECK "$LOG_CPPCHECK" "cppcheck (performance/security findings)"
bold "cppcheck lane: OK."
bg_join PID_BENCHBUILD "$LOG_BENCHBUILD" "release benchmark build"

# 8. Refresh the editor (best-effort, NOT a gate) ----------------------------
#    Launched AFTER the extension-check join (they share docs/xml + the hover DB) and
#    joined before the measured perf gate below. Never fails the gate: it no-ops if
#    `code`/vsce are unavailable (headless CI), and errors are swallowed.
if [ "${QA_GATE_SKIP_EDITOR:-0}" = "1" ]; then
    bold "Skipping editor refresh (QA_GATE_SKIP_EDITOR=1)."
else
    bold "Refreshing the VS Code extension from the freshly-built runtime (background)…"
    bg_launch PID_EDITOR "$LOG_EDITOR" bash editors/vscode/scripts/install-extension.sh
fi

# 7. Benchmarks: smoke run, sharded (hard gate that every case builds & runs) -
bold "Running benchmarks (smoke pass, sharded, min-time ${MIN_TIME})…"
bash scripts/bench_smoke.sh || fail "benchmark run"

# Editor refresh must finish before the MEASURED gate below gets the machine to itself.
if [ -n "$PID_EDITOR" ]; then
    _pid_editor="$PID_EDITOR"; PID_EDITOR=""
    wait "$_pid_editor" || printf '[qa-gate] editor refresh skipped/failed (non-fatal).\n'
fi

# 1 (verdict). Coverage finish + the verbatim checks from the sequential gate.
if [ "${QA_GATE_SKIP_COVERAGE:-0}" != "1" ]; then
    bg_join PID_COVFIN "$LOG_COVFIN" "coverage report"
    if ! git diff --quiet -- README.md; then
        printf '\n[qa-gate] The README coverage table is out of date. Updated it to:\n\n'
        git --no-pager diff -- README.md | sed -n '/coverage:start/,/coverage:end/p'
        fail "README coverage table changed — 'git add README.md && git commit', then push again"
    fi
    cat "$LOG_COVFIN"
    # Hard gate: line + function coverage must be exactly 100%.
    covnums=$(sed -n 's/.*lines [0-9.]*% (\([0-9]*\)\/\([0-9]*\)), functions [0-9.]*% (\([0-9]*\)\/\([0-9]*\)).*/\1 \2 \3 \4/p' "$LOG_COVFIN")
    [ -n "$covnums" ] || fail "could not parse the coverage summary (coverage.sh output changed?)"
    read -r lcov_n lcov_d fcov_n fcov_d <<<"$covnums"
    if [ "$lcov_n" != "$lcov_d" ] || [ "$fcov_n" != "$fcov_d" ]; then
        fail "unit-test coverage below 100% — lines $lcov_n/$lcov_d, functions $fcov_n/$fcov_d (find gaps with: scripts/coverage.sh show <file>)"
    fi
    bold "Unit-test coverage: 100% lines ($lcov_n/$lcov_d) + functions ($fcov_n/$fcov_d)."
fi

# 7b. Fixed-vs-GLM performance gate (hard gate) ------------------------------
#     linalg::Fixed exists to match GLM at GLM's own game; assert it still does, so a change that
#     quietly de-vectorizes a hot path fails here rather than in a consumer's frame budget. Tolerant
#     by ratio AND absolute gap, with a confirmation re-run, so sub-nanosecond noise never flakes it.
#     Runs LAST, with every lane joined — the measured passes get a quiet machine.
bold "Performance gate: linalg::Fixed vs GLM…"
./scripts/bench_gate.sh || fail "linalg::Fixed regressed against GLM"

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
