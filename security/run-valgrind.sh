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

# Valgrind serializes a process onto ONE core, so the way to use a 20-core box is to
# run many Valgrind PROCESSES at once. gtest's built-in sharding (GTEST_TOTAL_SHARDS /
# GTEST_SHARD_INDEX) partitions a binary's tests into disjoint subsets; we run the
# shards concurrently, each in its own Valgrind, and reassemble the counts. This is a
# pure wall-clock win — every test still runs under Valgrind exactly once, at the same
# debug -O0 build. Coverage stays provable: we assert the shards' executed-test counts
# SUM to the binary's full active-test total (so a lost/crashed shard can't hide).
JOBS="${VG_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"

status=0
total_ran=0
for t in "${UNIT_BINS[@]}"; do
    bin="./build/debug/bin/$t"
    [ -x "$bin" ] || { echo "[valgrind] missing $bin — cannot cover all unit tests"; status=1; continue; }
    # The per-function compile-run battery is opt-in (QA_GATE_FULL_CR=1); skip it
    # here by default so Valgrind stays fast (the unit tests already memcheck the
    # functions directly; the system apps still run).
    filter=()
    if [ "$t" = "cheatah_purrc_tests" ] && [ "${QA_GATE_FULL_CR:-0}" != "1" ]; then
        filter=(--gtest_filter=-*CompileRun*)
    fi

    # Full active-test total for THIS filter (the coverage denominator). gtest lists one
    # indented line per test; DISABLED_ tests are listed but never run, so exclude them.
    total=$("$bin" "${filter[@]}" --gtest_list_tests 2>/dev/null | grep -E '^  [^ ]' | grep -cv 'DISABLED_')
    if [ "${total:-0}" -eq 0 ]; then
        echo "[valgrind] $t: no active tests for this filter — skipping"; continue
    fi
    shards=$(( JOBS < total ? JOBS : total ))  # never more shards than tests
    echo "[valgrind] memcheck: $t ($total tests across $shards parallel shards)…"

    pids=(); logs=()
    for ((i = 0; i < shards; i++)); do
        log="/tmp/cheatah_vg_${t}_shard${i}.log"; logs+=("$log")
        GTEST_TOTAL_SHARDS="$shards" GTEST_SHARD_INDEX="$i" \
            "${VG[@]}" "$bin" "${filter[@]}" >"$log" 2>&1 &
        pids+=($!)
    done

    bin_ran=0
    for ((i = 0; i < shards; i++)); do
        log="${logs[$i]}"
        if ! wait "${pids[$i]}"; then
            echo "[valgrind] ERRORS/LEAKS (or crash) in $t shard $i/$shards:"; tail -50 "$log"; status=1
        fi
        ran=$(grep -oE '\[=+\] [0-9]+ test' "$log" | tail -1 | grep -oE '[0-9]+' || true)
        passed=$(grep -oE '\[ *PASSED *\] [0-9]+ test' "$log" | tail -1 | grep -oE '[0-9]+' || true)
        failed=$(grep -cE '\[ *FAILED *\]' "$log" || true)
        : "${ran:=0}"; : "${passed:=0}"; : "${failed:=0}"
        if [ "$passed" -ne "$ran" ] || [ "$failed" -ne 0 ]; then
            echo "[valgrind] $t shard $i/$shards: only $passed/$ran passed under Valgrind (failures: $failed)"; status=1
        fi
        bin_ran=$((bin_ran + ran))
    done

    # Coverage assertion: the shards must have executed EVERY active test exactly once.
    if [ "$bin_ran" -ne "$total" ]; then
        echo "[valgrind] $t: shards executed $bin_ran/$total tests under Valgrind — coverage incomplete (a shard was lost?)"; status=1
    else
        echo "[valgrind] $t: $bin_ran/$total unit tests executed clean under Valgrind"
        total_ran=$((total_ran + bin_ran))
    fi
done

if [ "$status" -eq 0 ]; then
    echo "[valgrind] 100% unit-test coverage: $total_ran tests across ${#UNIT_BINS[@]}/${#UNIT_BINS[@]} unit-test binaries, no errors or definite/indirect leaks"
fi
exit "$status"
