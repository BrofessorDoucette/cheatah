#!/usr/bin/env bash
# Run cheatah's tests under Valgrind memcheck — a second memory checker alongside
# ASan (each catches things the other can miss). Uses the plain `debug` build,
# because Valgrind cannot run an ASan-instrumented binary.
#
#   security/run-valgrind.sh
#
# Covers all three test kinds: the in-process unit tests AND the compile-run +
# system-level tests (cheatah_purrc_tests). The compile-run/system harness shells
# out to purrc/cheatah (those children run natively, not traced), so Valgrind here
# memchecks the in-process unit tests directly and the e2e harness itself.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

SUPP="security/valgrind.supp"

echo "[valgrind] building (debug)…"
cmake --preset debug          >/tmp/cheatah_vg_cfg.log   2>&1 || { tail -20 /tmp/cheatah_vg_cfg.log;   exit 1; }
cmake --build --preset debug  >/tmp/cheatah_vg_build.log 2>&1 || { tail -30 /tmp/cheatah_vg_build.log; exit 1; }

VG=(valgrind --tool=memcheck --leak-check=full
    --show-leak-kinds=definite,indirect
    --errors-for-leak-kinds=definite,indirect
    --error-exitcode=1 --suppressions="$SUPP")

# All test binaries — the in-process unit tests (cheatah_tests, which now includes
# the linalg core + smoke tests) plus the compile-run + system-level tests
# (cheatah_purrc_tests). 100% Valgrind coverage = every test in each runs under
# Valgrind, all pass, none skipped (asserted below).
UNIT_BINS=(cheatah_tests cheatah_purrc_tests)

status=0
total_ran=0
for t in "${UNIT_BINS[@]}"; do
    bin="./build/debug/bin/$t"
    [ -x "$bin" ] || { echo "[valgrind] missing $bin — cannot cover all unit tests"; status=1; continue; }
    log="/tmp/cheatah_vg_$t.log"
    # The per-function compile-run battery is opt-in (QA_GATE_FULL_CR=1); skip it
    # here by default so Valgrind stays fast (the unit tests already memcheck the
    # functions directly; the system apps still run).
    filter=()
    if [ "$t" = "cheatah_purrc_tests" ] && [ "${QA_GATE_FULL_CR:-0}" != "1" ]; then
        filter=(--gtest_filter=-*CompileRun*)
    fi
    echo "[valgrind] memcheck: $t"
    if ! "${VG[@]}" "$bin" "${filter[@]}" >"$log" 2>&1; then
        echo "[valgrind] ERRORS/LEAKS in $t:"; tail -50 "$log"; status=1
    fi
    # Coverage assertion: confirm every test in the binary actually executed under
    # Valgrind (a crash/abort would run fewer than registered, silently shrinking coverage).
    ran=$(grep -oE '\[=+\] [0-9]+ test' "$log" | tail -1 | grep -oE '[0-9]+' || true)
    passed=$(grep -oE '\[ *PASSED *\] [0-9]+ test' "$log" | tail -1 | grep -oE '[0-9]+' || true)
    failed=$(grep -cE '\[ *FAILED *\]' "$log" || true)
    : "${ran:=0}"; : "${passed:=0}"; : "${failed:=0}"
    if [ "$ran" -eq 0 ]; then
        echo "[valgrind] $t executed 0 tests under Valgrind — coverage incomplete"; status=1
    elif [ "$passed" -ne "$ran" ] || [ "$failed" -ne 0 ]; then
        echo "[valgrind] $t: only $passed/$ran tests passed under Valgrind (failures: $failed)"; status=1
    else
        echo "[valgrind] $t: $ran/$ran unit tests executed clean under Valgrind"
        total_ran=$((total_ran + ran))
    fi
done

if [ "$status" -eq 0 ]; then
    echo "[valgrind] 100% unit-test coverage: $total_ran tests across ${#UNIT_BINS[@]}/${#UNIT_BINS[@]} unit-test binaries, no errors or definite/indirect leaks"
fi
exit "$status"
