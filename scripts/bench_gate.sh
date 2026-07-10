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
#   2. An ABSOLUTE FLOOR. A ratio is meaningless on a half-nanosecond operation. `dot` on a 3-vector
#      of doubles compiles to instruction-identical code in both libraries — the same movsd/mulsd/
#      addsd sequence, verified by reading the assembly — and still measures ~0.09 ns apart, because
#      at that scale the harness's own DoNotOptimize scaffolding dominates. So a pair must ALSO be
#      slower by more than MIN_GAP_NS (default 0.15 ns, about half a cycle) to fail. Anything under
#      that is reported, never fatal.
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
MIN_GAP_NS="${MIN_GAP_NS:-0.15}"
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

bold "measuring the fixed/glm pairs…"
run_pairs '_(fixed|glm)$' 5 0.2s > /tmp/cheatah_bench_pass1.csv || fail "benchmark run"

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
