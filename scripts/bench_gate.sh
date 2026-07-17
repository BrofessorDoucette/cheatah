#!/usr/bin/env bash
# bench_gate.sh — cheatah::linalg::Fixed must never become slower than GLM.
#
# tests/benchmarks/fixed_glm_bench.cpp measures the COMPLETE overlap of the two APIs as
# BM_<op>_fixed / BM_<op>_glm pairs. This gate runs them and fails if any pair regresses. Without it,
# "Fixed is as fast as GLM" is a claim that was true once, on one commit, on one machine.
#
# Three things keep it from being a flaky gate:
#
#   1. A TOLERANCE. Most of these operations take one or two cycles, where measurement noise is a
#      larger effect than any code change; run-to-run sign flips of +/-5% are normal. A pair fails
#      only above THRESHOLD (default 1.15x).
#   2. An ABSOLUTE FLOOR. A ratio is meaningless on a sub-nanosecond operation. `dot` on a 3-vector
#      of doubles compiles to instruction-identical code in both libraries — the same movsd/mulsd/
#      addsd sequence, verified by reading the assembly — and still measures ~0.09 ns apart, because
#      at that scale the harness's own DoNotOptimize scaffolding dominates. So a pair must ALSO be
#      slower by more than MIN_GAP_NS (default 0.25 ns, about one cycle) to fail. Anything under
#      that is reported, never fatal. The floor is one cycle, not half, because in the QA gate the
#      benchmarks run LAST — after the ASan/TSan/Valgrind stages have pinned the CPU for ~15 min —
#      so boost clocks are depressed and the smallest ops (a 16-double mat4 add is ~1.4 ns) drift a
#      few tenths of a nanosecond hotter than a cold run; that thermal tail must not fail the build.
#   3. CONFIRMATION. Anything that trips both is re-measured, alone, with more repetitions. A single
#      noisy sample never fails the build; a real regression survives the second look.
#
#   scripts/bench_gate.sh              # gate (build if needed, then check)
#   THRESHOLD=1.10 scripts/bench_gate.sh
#   scripts/bench_gate.sh report       # print every pair, fail nothing
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

MODE="${1:-gate}"
THRESHOLD="${THRESHOLD:-1.15}"
MIN_GAP_NS="${MIN_GAP_NS:-0.25}"
BIN=build/release/bin/cheatah_benchmarks

bold() { printf '\n\033[1m[bench-gate] %s\033[0m\n' "$*"; }
skip() { printf '\033[33m[bench-gate] SKIP: %s\033[0m\n' "$*"; exit 0; }
fail() { printf '\033[31m[bench-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

command -v cmake >/dev/null 2>&1 || skip "no cmake"
if [ ! -x "$BIN" ]; then
    bold "building the benchmarks (release)…"
    cmake --preset release -DCHEATAH_BUILD_BENCHMARKS=ON >/tmp/cheatah_bench_cfg.log 2>&1 \
        || skip "benchmark configure failed (GLM missing?) — see /tmp/cheatah_bench_cfg.log"
    cmake --build --preset release --target cheatah_benchmarks -j"$(nproc)" >/tmp/cheatah_bench_build.log 2>&1 \
        || skip "benchmark build failed — see /tmp/cheatah_bench_build.log"
fi
[ -x "$BIN" ] || skip "no benchmark binary"

run_pairs() {  # run_pairs <filter> <reps> <min_time> -> csv on stdout
    "$BIN" --benchmark_filter="$1" --benchmark_repetitions="$2" --benchmark_min_time="$3" \
        --benchmark_report_aggregates_only=true --benchmark_format=csv 2>/dev/null
}

# ---- pass 1: the SCREEN, pair-sharded across cores --------------------------------------------
# Pass 1 only nominates suspects; pass 2 below re-measures them serially, alone, with more
# repetitions, and is the ONLY place a failure is declared. That authority split is what makes
# sharding the screen safe: concurrency noise can at worst nominate extra suspects, each of which
# pass 2 then measures under the same quiet conditions as always. A pair's _fixed and _glm case
# are kept in the SAME shard (the comparison is intra-pair, so shared-core noise hits both sides
# alike); glm-only orphans become one-name units, measured exactly as before. BENCH_GATE_JOBS=1
# reproduces the old single-process pass 1 verbatim. Default 8 (not nproc): a lightly-loaded box
# keeps the suspect rate near zero, and pass 1 is already ~6x faster at 8.
BENCH_GATE_JOBS="${BENCH_GATE_JOBS:-8}"
esc() { printf '%s' "$1" | sed 's/[][\.|$(){}?+*^\\]/\\&/g'; }

bold "measuring the fixed/glm pairs (pass 1 across ${BENCH_GATE_JOBS} shards)…"
# Names in this subset never contain whitespace (PAIR-macro generated, BM_<op>_<type>_<side>);
# if a future one did, the read below would mis-split and the completeness check fails loudly.
PAIR_NAMES=()
while IFS= read -r _n; do PAIR_NAMES+=("$_n"); done < <("$BIN" --benchmark_list_tests | grep -E '_(fixed|glm)$')
[ "${#PAIR_NAMES[@]}" -gt 0 ] || fail "no fixed/glm benchmark cases listed"

# Group names into pair units: key = name minus its _fixed/_glm suffix. A stable sort by key
# makes a pair's two sides ADJACENT (fixed before glm, registration order preserved within a
# key), so one walk builds each unit's alternation. Plain indexed arrays only — macOS stock
# bash is 3.2 (no associative arrays) and the gate runs there too.
UNIT_FILTERS=(); _prev_key=""
while read -r _k _n; do
    _e=$(esc "$_n")
    if [ "$_k" = "$_prev_key" ]; then
        _last=$(( ${#UNIT_FILTERS[@]} - 1 ))
        UNIT_FILTERS[_last]="${UNIT_FILTERS[_last]}|$_e"
    else
        UNIT_FILTERS+=("$_e"); _prev_key="$_k"
    fi
done < <(for _n in "${PAIR_NAMES[@]}"; do
             _k="${_n%_fixed}"; _k="${_k%_glm}"; printf '%s %s\n' "$_k" "$_n"
         done | sort -s -k1,1)
shards=$(( BENCH_GATE_JOBS < ${#UNIT_FILTERS[@]} ? BENCH_GATE_JOBS : ${#UNIT_FILTERS[@]} ))
SHARD_FILTER=()
for ((i = 0; i < shards; i++)); do SHARD_FILTER[i]=""; done
for ((i = 0; i < ${#UNIT_FILTERS[@]}; i++)); do
    s=$((i % shards))
    SHARD_FILTER[s]="${SHARD_FILTER[s]}${SHARD_FILTER[s]:+|}${UNIT_FILTERS[i]}"
done

pass1_pids=()
for ((i = 0; i < shards; i++)); do
    : > "/tmp/cheatah_bench_pass1_shard$i.csv"   # truncate: a lost shard must yield 0 rows
    run_pairs "^(${SHARD_FILTER[i]})"'$' 5 0.2s > "/tmp/cheatah_bench_pass1_shard$i.csv" &
    pass1_pids+=($!)
done
for p in "${pass1_pids[@]}"; do wait "$p" || fail "benchmark run"; done
: > /tmp/cheatah_bench_pass1.csv
for ((i = 0; i < shards; i++)); do cat "/tmp/cheatah_bench_pass1_shard$i.csv" >> /tmp/cheatah_bench_pass1.csv; done
# Completeness: one _median aggregate row per case; the union must equal the listed set.
measured=$(grep -cE '^"?BM_[^,]*_median"?,' /tmp/cheatah_bench_pass1.csv || true)
[ "${measured:-0}" -eq "${#PAIR_NAMES[@]}" ] || \
    fail "pass 1 incomplete: measured ${measured:-0}/${#PAIR_NAMES[@]} fixed/glm cases (a shard was lost?)"

SUSPECTS="$(python3 - "$THRESHOLD" "$MIN_GAP_NS" <<'PY'
import csv, sys
threshold, min_gap = float(sys.argv[1]), float(sys.argv[2])
t = {}
for row in csv.reader(open('/tmp/cheatah_bench_pass1.csv')):
    if not row or not row[0].startswith('BM_') or not row[0].endswith('_median'):
        continue
    name, ns = row[0][:-len('_median')], float(row[2])
    if name.endswith('_fixed'):
        t.setdefault(name[3:-6], {})['f'] = ns
    elif name.endswith('_glm'):
        t.setdefault(name[3:-4], {})['g'] = ns
pairs = {k: v for k, v in t.items() if 'f' in v and 'g' in v}
if not pairs:
    sys.stderr.write("no fixed/glm pairs found — did the benchmark names change?\n")
    sys.exit(2)
print(' '.join(k for k, v in pairs.items()
            if v['g'] > 0 and v['f'] / v['g'] > threshold and (v['f'] - v['g']) > min_gap))
PY
)" || fail "could not parse the benchmark output"

if [ -z "$SUSPECTS" ]; then
    printf '\n\033[32m[bench-gate] OK — Fixed is within %sx (or %s ns) of GLM on every operation.\033[0m\n' "$THRESHOLD" "$MIN_GAP_NS"
    exit 0
fi

bold "re-measuring under suspicion (noise, or a real regression?): $SUSPECTS"
FILTER="BM_($(echo "$SUSPECTS" | tr ' ' '|'))_(fixed|glm)$"
run_pairs "$FILTER" 15 0.5s > /tmp/cheatah_bench_pass2.csv || fail "confirmation run"

python3 - "$THRESHOLD" "$MIN_GAP_NS" "$MODE" <<'PY' || fail "Fixed regressed against GLM (see above)"
import csv, sys
threshold, min_gap, mode = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
t = {}
for row in csv.reader(open('/tmp/cheatah_bench_pass2.csv')):
    if not row or not row[0].startswith('BM_') or not row[0].endswith('_median'):
        continue
    name, ns = row[0][:-len('_median')], float(row[2])
    if name.endswith('_fixed'):
        t.setdefault(name[3:-6], {})['f'] = ns
    elif name.endswith('_glm'):
        t.setdefault(name[3:-4], {})['g'] = ns

bad = []
for k, v in sorted(t.items()):
    if 'f' not in v or 'g' not in v or v['g'] <= 0:
        continue
    ratio, gap = v['f'] / v['g'], v['f'] - v['g']
    regressed = ratio > threshold and gap > min_gap
    verdict = 'REGRESSED' if regressed else ('below-floor' if ratio > threshold else 'noise')
    print(f"    {verdict:12s} {k:22s} fixed {v['f']:8.3f} ns   glm {v['g']:8.3f} ns   {ratio:.2f}x  (+{gap:.3f} ns)")
    if regressed:
        bad.append((k, ratio, gap))

if bad and mode != 'report':
    print()
    for k, ratio, gap in bad:
        print(f"  {k} is {ratio:.2f}x GLM (+{gap:.3f} ns) and stayed that way under 15 repetitions.")
    sys.exit(1)
PY

printf '\n\033[32m[bench-gate] OK — every flagged pair was noise; Fixed holds against GLM.\033[0m\n'
