#!/usr/bin/env bash
# bench_smoke.sh — the "benchmarks build & run" hard gate, sharded across cores.
#
# The QA gate's smoke pass proves every registered benchmark case still builds and
# executes; its TIMINGS ARE DISCARDED (the perf authorities are bench_gate.sh and
# compiler_bench_gate.sh, which measure on a quiet machine). Running all ~441 cases
# serially in one process cost ~4-5 minutes of the gate; sharding them across
# concurrent processes is a pure wall-clock win — every case still runs exactly once,
# only the ignored nanosecond numbers get noisier.
#
# Mirrors security/run-valgrind.sh's completeness pattern: partition the full case
# list, run shards concurrently, and REQUIRE (a) every shard exits 0, (b) each shard
# executed exactly the cases assigned to it, and (c) the total across shards equals
# the listed count — so a lost or crashed shard fails the stage rather than silently
# shrinking what was smoked.
#
#   scripts/bench_smoke.sh                 # shard across nproc
#   BENCH_SMOKE_JOBS=1 scripts/bench_smoke.sh   # single shard == today's serial run
#   QA_BENCH_MIN_TIME=0.05s                # per-case min time (same knob as the gate)
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

BIN=build/release/bin/cheatah_benchmarks
MIN_TIME="${QA_BENCH_MIN_TIME:-0.05s}"
JOBS="${BENCH_SMOKE_JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"

[ -x "$BIN" ] || { echo "[bench-smoke] missing $BIN (build release-benchmarks first)"; exit 1; }

# The full expanded case list — computed live, never hard-coded, so new benchmarks
# are automatically covered and a registration regression (fewer cases) is visible.
mapfile -t ALL < <("$BIN" --benchmark_list_tests)
total=${#ALL[@]}
[ "$total" -gt 0 ] || { echo "[bench-smoke] no benchmark cases listed"; exit 1; }
shards=$(( JOBS < total ? JOBS : total ))

# Escape regex metacharacters in a case name for the exact-name alternation filter.
# Today's names contain none (verified), but stay defensive for future registrations.
esc() { printf '%s' "$1" | sed 's/[][\.|$(){}?+*^\\]/\\&/g'; }

# Round-robin partition (spreads the heavy SVD/eig/crypto cases across shards).
declare -a FILTER COUNT
for ((i = 0; i < shards; i++)); do FILTER[i]=""; COUNT[i]=0; done
for ((i = 0; i < total; i++)); do
    s=$((i % shards))
    FILTER[s]+="${FILTER[s]:+|}$(esc "${ALL[i]}")"
    COUNT[s]=$((COUNT[s] + 1))
done

pids=(); logs=()
for ((i = 0; i < shards; i++)); do
    log="/tmp/cheatah_bench_smoke_shard${i}.log"; logs+=("$log")
    : >"$log"   # truncate first: a crashed shard must yield 0 counted cases, not a stale log's
    "$BIN" --benchmark_filter="^(${FILTER[i]})"'$' --benchmark_min_time="$MIN_TIME" >"$log" 2>&1 &
    pids+=($!)
done

status=0; ran_total=0
for ((i = 0; i < shards; i++)); do
    if ! wait "${pids[i]}"; then
        echo "[bench-smoke] shard $i/$shards FAILED:"; tail -30 "${logs[i]}"; status=1
    fi
    # One console result line per executed case (colors are off when not a tty).
    ran=$(grep -c '^BM_' "${logs[i]}" || true); : "${ran:=0}"
    if [ "$ran" -ne "${COUNT[i]}" ]; then
        echo "[bench-smoke] shard $i/$shards executed $ran/${COUNT[i]} assigned cases"; status=1
    fi
    ran_total=$((ran_total + ran))
done

if [ "$ran_total" -ne "$total" ]; then
    echo "[bench-smoke] executed $ran_total/$total cases — coverage incomplete (a shard was lost?)"
    status=1
elif [ "$status" -eq 0 ]; then
    echo "[bench-smoke] $ran_total/$total benchmark cases executed clean across $shards shards"
fi
exit "$status"
