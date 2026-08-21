#!/usr/bin/env bash
# compiler_bench_gate.sh — the cheatah frontend (lexer/parser/codegen) must not get slower.
#
# tests/benchmarks/compiler_bench.cpp times each transpilation stage over small/medium/large
# real .purr. Unlike bench_gate.sh (which compares two live implementations, Fixed vs GLM, in the
# SAME run) the compiler has no reference peer, so this gate compares the CURRENT run against a
# COMMITTED baseline median (tests/benchmarks/compiler_bench_baseline.csv). The baseline is the
# speed we refuse to fall below; it is refreshed (a deliberate, reviewed step: `update`) whenever a
# real optimization lands, so the ratchet only ever moves faster.
#
# The same three anti-flake mechanisms as bench_gate.sh:
#   1. TOLERANCE  — a stage fails only above THRESHOLD (default 1.10x). Run-to-run noise is real.
#   2. ABSOLUTE FLOOR — and only if it also slowed by more than MIN_GAP_NS (default 200 ns); a tiny
#      absolute drift on the fastest stage (parse/small ~1.5 us) is never fatal.
#   3. CONFIRMATION — anything tripping both is re-measured alone with more repetitions before failing.
#
# NOTE: a committed baseline is inherently cross-machine / cross-time, so this is a DEV + same-machine
# before/after tool, not a hard push gate (the golden-master is the hard correctness gate). It SKIPs
# cleanly when there is no baseline yet.
#
#   scripts/compiler_bench_gate.sh            # gate: fail on a confirmed regression vs baseline
#   scripts/compiler_bench_gate.sh report     # print every stage vs baseline, fail nothing
#   scripts/compiler_bench_gate.sh update      # (re)capture the baseline from a fresh run
#   THRESHOLD=1.05 scripts/compiler_bench_gate.sh
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

MODE="${1:-gate}"
THRESHOLD="${THRESHOLD:-1.10}"
MIN_GAP_NS="${MIN_GAP_NS:-200}"
BIN=build/release/bin/cheatah_benchmarks
BASELINE="${BASELINE:-tests/benchmarks/compiler_bench_baseline.csv}"
FILTER='^BM_(tokenize|parse|codegen|frontend)/'

bold() { printf '\n\033[1m[compiler-bench-gate] %s\033[0m\n' "$*"; }

# The header comment above admits a committed baseline is inherently cross-machine. That was
# true and unenforced: a baseline captured on someone else's laptop compared silently against
# yours, and every ratio it produced was noise dressed as a verdict. Stamp the machine into
# the baseline and compare it, so the mismatch is announced rather than absorbed.
machine_stamp() {
    local cpu="unknown"
    [ -r /proc/cpuinfo ] && cpu="$(awk -F': ' '/^model name/{print $2; exit}' /proc/cpuinfo)"
    [ -n "$cpu" ] || cpu="unknown"
    printf '%s | %s cpus | %s %s' "$cpu" "$(nproc 2>/dev/null || echo '?')" \
        "$(uname -s)" "$(uname -r)"
}
skip() { printf '\033[33m[compiler-bench-gate] SKIP: %s\033[0m\n' "$*"; exit 0; }
fail() { printf '\033[31m[compiler-bench-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

command -v cmake >/dev/null 2>&1 || skip "no cmake"
if [ ! -x "$BIN" ]; then
    bold "building the benchmarks (release)…"
    cmake --preset release >/tmp/cheatah_cbench_cfg.log 2>&1 \
        || skip "benchmark configure failed — see /tmp/cheatah_cbench_cfg.log"
    cmake --build --preset release --target cheatah_benchmarks -j"$(nproc)" >/tmp/cheatah_cbench_build.log 2>&1 \
        || skip "benchmark build failed — see /tmp/cheatah_cbench_build.log"
fi
[ -x "$BIN" ] || skip "no benchmark binary"

run() {  # run <reps> <min_time> -> median CSV on stdout
    # Interleaved for the same reason as scripts/bench_gate.sh: the repetitions of each stage
    # are scattered across the run, so drift over the measurement window becomes zero-mean
    # noise the median absorbs rather than a systematic shift on whichever stage ran last.
    "$BIN" --benchmark_filter="$FILTER" --benchmark_repetitions="$1" --benchmark_min_time="$2" \
        --benchmark_enable_random_interleaving=true \
        --benchmark_report_aggregates_only=true --benchmark_format=csv 2>/dev/null
}

# medians <csv-file> -> "name ns" lines (one per BM_*_median row).
medians() {
    python3 - "$1" <<'PY'
import csv, sys
for row in csv.reader(open(sys.argv[1])):
    if row and row[0].startswith('BM_') and row[0].endswith('_median'):
        print(row[0][:-len('_median')], row[2])
PY
}

# ---- machine check: a cross-machine baseline is a warning, never a silent comparison ----------
check_machine() {
    local recorded
    recorded="$(awk -F'machine: *' '/^#   machine:/{print $2; exit}' "$BASELINE" 2>/dev/null)"
    if [ -z "$recorded" ]; then
        printf '\033[33m[compiler-bench-gate] NOTE: baseline predates machine stamping — it may '
        printf 'have been captured elsewhere. Re-run `update` on this machine to be sure.\033[0m\n'
    elif [ "$recorded" != "$(machine_stamp)" ]; then
        printf '\033[33m[compiler-bench-gate] WARNING: baseline was captured on a DIFFERENT machine.\033[0m\n'
        printf '  baseline: %s\n  current:  %s\n' "$recorded" "$(machine_stamp)"
        printf '  Ratios below compare two machines and mean very little. Run `update` here first.\n'
    fi
}

# ---- update: capture the baseline and exit ----------------------------------------------------
if [ "$MODE" = "update" ]; then
    bold "capturing the compiler-bench baseline (5 reps)…"
    run 5 0.05s > /tmp/cheatah_cbench_update.csv || fail "benchmark run"
    { echo "# cheatah frontend speed baseline — regenerate with: scripts/compiler_bench_gate.sh update"
      echo "# cheatah-bench-stamp v1"
      echo "#   generated:   $(date -u +%Y-%m-%d)"
      echo "#   commit:      $(git rev-parse --short HEAD)$(git diff --quiet || echo ' (dirty)')"
      echo "#   machine:     $(machine_stamp)"
      echo "#   harness:     reps=5, min_time=0.05s, random-interleaving=on"
      echo "#   statistic:   median over repetitions"
      echo "#   publishable: comparison-only — a ratchet baseline, not a figure to quote"
      echo "# name,median_ns"
      medians /tmp/cheatah_cbench_update.csv | awk '{print $1","$2}' | sort
    } > "$BASELINE" || fail "could not write $BASELINE"
    printf '\033[32m[compiler-bench-gate] wrote baseline %s (%s stages)\033[0m\n' \
        "$BASELINE" "$(grep -c '^BM_' "$BASELINE")"
    exit 0
fi

[ -f "$BASELINE" ] || skip "no baseline yet ($BASELINE) — capture it with: scripts/compiler_bench_gate.sh update"

check_machine
bold "measuring the frontend stages…"
run 5 0.05s > /tmp/cheatah_cbench_pass1.csv || fail "benchmark run"

# Compare pass1 vs baseline; emit suspect stage names (space-separated).
SUSPECTS="$(python3 - "$THRESHOLD" "$MIN_GAP_NS" "$BASELINE" /tmp/cheatah_cbench_pass1.csv <<'PY'
import csv, sys
threshold, min_gap, base_path, cur_path = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3], sys.argv[4]
def load_base(p):
    d = {}
    for line in open(p):
        line = line.strip()
        if not line or line.startswith('#'): continue
        name, ns = line.rsplit(',', 1)
        d[name] = float(ns)
    return d
def load_cur(p):
    d = {}
    for row in csv.reader(open(p)):
        if row and row[0].startswith('BM_') and row[0].endswith('_median'):
            d[row[0][:-len('_median')]] = float(row[2])
    return d
base, cur = load_base(base_path), load_cur(cur_path)
common = [k for k in cur if k in base and base[k] > 0]
if not common:
    sys.stderr.write("no overlapping stages between baseline and current — names changed?\n"); sys.exit(2)
print(' '.join(k for k in common if cur[k]/base[k] > threshold and (cur[k]-base[k]) > min_gap))
PY
)" || fail "could not parse the benchmark output"

if [ -z "$SUSPECTS" ] && [ "$MODE" != "report" ]; then
    printf '\n\033[32m[compiler-bench-gate] OK — every frontend stage is within %sx (or %s ns) of baseline.\033[0m\n' \
        "$THRESHOLD" "$MIN_GAP_NS"
fi

# In report mode, always run the confirm pass so the full table prints; in gate mode, only when suspect.
if [ -n "$SUSPECTS" ] || [ "$MODE" = "report" ]; then
    if [ -n "$SUSPECTS" ]; then
        bold "re-measuring under suspicion (noise, or a real regression?): $SUSPECTS"
    else
        bold "report: re-measuring all stages…"
    fi
    run 15 0.2s > /tmp/cheatah_cbench_pass2.csv || fail "confirmation run"

    python3 - "$THRESHOLD" "$MIN_GAP_NS" "$MODE" "$BASELINE" /tmp/cheatah_cbench_pass2.csv <<'PY' || fail "the frontend regressed vs baseline (see above)"
import csv, sys
threshold, min_gap, mode, base_path, cur_path = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
def load_base(p):
    d = {}
    for line in open(p):
        line = line.strip()
        if not line or line.startswith('#'): continue
        name, ns = line.rsplit(',', 1)
        d[name] = float(ns)
    return d
base = load_base(base_path)
cur = {}
for row in csv.reader(open(cur_path)):
    if row and row[0].startswith('BM_') and row[0].endswith('_median'):
        cur[row[0][:-len('_median')]] = float(row[2])
bad = []
for k in sorted(cur):
    if k not in base or base[k] <= 0: continue
    ratio, gap = cur[k]/base[k], cur[k]-base[k]
    regressed = ratio > threshold and gap > min_gap
    verdict = 'REGRESSED' if regressed else ('below-floor' if ratio > threshold else ('faster' if ratio < 0.98 else 'ok'))
    print(f"    {verdict:12s} {k:24s} now {cur[k]:10.1f} ns   base {base[k]:10.1f} ns   {ratio:.2f}x  ({gap:+.1f} ns)")
    if regressed: bad.append((k, ratio, gap))
if bad and mode != 'report':
    print()
    for k, ratio, gap in bad:
        print(f"  {k} is {ratio:.2f}x baseline (+{gap:.1f} ns) and stayed that way under 15 repetitions.")
    sys.exit(1)
PY
fi

printf '\n\033[32m[compiler-bench-gate] OK — the frontend holds against baseline.\033[0m\n'