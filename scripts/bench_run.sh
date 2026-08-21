#!/usr/bin/env bash
# bench_run.sh — the ONE way to invoke cheatah_benchmarks.
#
# Google Benchmark's defaults are a trap for a project that publishes numbers: run the binary
# bare and you get ONE repetition, no statistics, no interleaving, and output that looks
# exactly as authoritative as a careful measurement. Every caller in this repo used to pass
# its own ad-hoc flag set, and whether a given number was rigorous depended on which script
# happened to produce it.
#
# So there are three named profiles, and the name says what the numbers are worth:
#
#   smoke    1 rep, 0.05s, no interleaving.  DOES THIS RUN AT ALL — timings are discarded.
#            The QA gate's pass (scripts/bench_smoke.sh) lives here, and its speed is a
#            deliberate property: 447 cases must not cost the gate minutes for statistics
#            nobody reads. bench_main.cpp is told to skip its "not publishable" banner.
#
#   gate     5 reps (15 on confirmation), interleaved, aggregates only. IS THIS A REGRESSION —
#            enough repetitions for a stable median, and the pair-sides-interleaved ordering
#            that keeps drift from masquerading as a ratio change.
#
#   publish  N reps, interleaved, all samples kept, stamped Markdown emitted. NUMBERS THAT
#            GO IN A DOC. Interleaving scatters every case's repetitions across the run, so a
#            pair's two sides are never measured as consecutive blocks with drift between
#            them. Each row is a median with its IQR. This is the profile bench_main.cpp
#            stamps `publishable: true`, and the only one whose output may reach a document.
#
# Usage:
#   scripts/bench_run.sh smoke   [filter]
#   scripts/bench_run.sh gate    [filter] [reps] [min_time]
#   scripts/bench_run.sh publish <suite-name> [filter] [reps]
#
# publish writes the stamped Markdown table to docs/bench/<suite-name>.md and the raw
# Google Benchmark JSON beside it as docs/bench/<suite-name>.json.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

BIN=build/release/bin/cheatah_benchmarks
PROFILE="${1:-}"

bold() { printf '\n\033[1m[bench-run] %s\033[0m\n' "$*"; }
die()  { printf '\033[31m[bench-run] %s\033[0m\n' "$*" >&2; exit 1; }

[ -n "$PROFILE" ] || die "usage: bench_run.sh {smoke|gate|publish} …"
if [ ! -x "$BIN" ]; then
    bold "building the benchmarks (release)…"
    cmake --preset release -DCHEATAH_BUILD_BENCHMARKS=ON >/tmp/bench_run_cfg.log 2>&1 \
        || die "configure failed — see /tmp/bench_run_cfg.log"
    cmake --build --preset release --target cheatah_benchmarks -j"$(nproc)" \
        >/tmp/bench_run_build.log 2>&1 || die "build failed — see /tmp/bench_run_build.log"
fi

# The commit reaches bench_main.cpp by environment rather than a compile definition: a -D
# baked in at configure time silently goes stale, and a stamp naming the wrong commit is
# worse than one admitting it does not know. `dirty` is recorded, and the doc lint refuses
# to publish a table stamped from a dirty tree.
COMMIT="$(git rev-parse --short HEAD)"
# Dirtiness of the SOURCES, not of docs/bench. Regenerating an artifact necessarily rewrites
# docs/bench/<suite>.md, so a plain `git diff --quiet` marks every suite after the first as
# dirty — measuring twice would make the second measurement look untraceable. What the stamp
# claims is that THIS COMMIT's source produced the number, and rewriting an output does not
# change the source. `update-index --refresh` first: stale stat info alone makes --quiet
# report a difference that is not there.
git update-index --refresh >/dev/null 2>&1 || true
git diff --quiet -- . ':!docs/bench' || COMMIT="$COMMIT (dirty)"
export CHEATAH_BENCH_COMMIT="$COMMIT"

case "$PROFILE" in
smoke)
    FILTER="${2:-.*}"
    export CHEATAH_BENCH_SMOKE=1
    exec "$BIN" --benchmark_filter="$FILTER" --benchmark_min_time=0.05s
    ;;

gate)
    FILTER="${2:-.*}"
    REPS="${3:-5}"
    MIN_TIME="${4:-0.2s}"
    exec "$BIN" --benchmark_filter="$FILTER" \
        --benchmark_repetitions="$REPS" --benchmark_min_time="$MIN_TIME" \
        --benchmark_enable_random_interleaving=true \
        --benchmark_report_aggregates_only=true --benchmark_format=csv
    ;;

publish)
    SUITE="${2:?publish needs a suite name: bench_run.sh publish <suite> [filter] [reps]}"

    # ---- the suite registry -------------------------------------------------------------
    # THIS BLOCK IS THE DEFINITION OF EVERY PUBLISHED TABLE. Filter, layout, watched sources
    # and (for curated tables) the exact row selection all live here, so `bench_run.sh publish
    # <suite>` is a complete, reviewable command and re-running it reproduces the document.
    # Curation that lives in a Markdown edit cannot be reproduced or reviewed; curation that
    # lives here shows up in a diff.
    #
    # WATCH is what scripts/bench_table.purr uses to decide the table has gone stale: list the
    # sources whose change invalidates the measurement. Too narrow and staleness never fires;
    # too broad and it cries wolf on every commit. Aim at the code the benchmark exercises.
    CHEATAH_BENCH_LAYOUT=""; CHEATAH_BENCH_WATCH=""; CHEATAH_BENCH_ROWS=""
    case "$SUITE" in
    crypto-vs-openssl)
        SUITE_FILTER='Crypto'
        CHEATAH_BENCH_LAYOUT=throughput
        CHEATAH_BENCH_WATCH='stdlib/aead/, stdlib/hashlib/, tests/benchmarks/crypto_openssl_bench.cpp'
        ;;
    fixarray-vs-glm)
        SUITE_FILTER='_(fixed|glm)$'
        CHEATAH_BENCH_LAYOUT=opstype
        CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp'
        ;;
    fixarray-vs-glm-highlights)
        # Narrowed to exactly the curated cases: the full fixed/glm sweep is ~280 cases and
        # this table shows six of them, so filtering here turns a six-minute run into seconds
        # without changing a single published number.
        SUITE_FILTER='^BM_(identity_mat4f|matmul_mat4f|add_mat4f|inverse_mat4d|abs_vec4f|dot_vec4f)_(fixed|glm)$'
        CHEATAH_BENCH_LAYOUT=highlights
        CHEATAH_BENCH_WATCH='stdlib/fixarray/, tests/benchmarks/fixed_glm_bench.cpp'
        # Six operations that show where structure pays, plus two deliberately kept because
        # they land INSIDE the parity band — a highlights table that only showed wins would
        # be a different kind of dishonest.
        CHEATAH_BENCH_ROWS='BM_identity_mat4f=mat4f::identity();BM_matmul_mat4f=mat4f * mat4f;BM_add_mat4f=mat4f + mat4f;BM_inverse_mat4d=inverse(mat4d);BM_abs_vec4f=abs(vec4f);BM_dot_vec4f=dot(vec4f, vec4f)'
        ;;
    linalg-vs-eigen)
        SUITE_FILTER='^BM_(dot|matmul|solve|inv)_(cheatah|eigen)'
        CHEATAH_BENCH_LAYOUT=pairs
        CHEATAH_BENCH_WATCH='stdlib/linalg/, tests/benchmarks/eigen_compare_bench.cpp'
        ;;
    p256)
        SUITE_FILTER='^BM_P256_'
        CHEATAH_BENCH_LAYOUT=solo
        CHEATAH_BENCH_WATCH='stdlib/p256/, tests/benchmarks/p256_bench.cpp'
        ;;
    *)
        # An unregistered suite is allowed for ad-hoc exploration, but it gets no layout and
        # no watch — so bench_table.purr will refuse to publish it, which is the right default.
        SUITE_FILTER='.*'
        printf '\033[33m[bench-run] %s is not in the suite registry — ad-hoc run, not publishable\033[0m\n' "$SUITE"
        ;;
    esac
    export CHEATAH_BENCH_LAYOUT CHEATAH_BENCH_WATCH CHEATAH_BENCH_ROWS

    FILTER="${3:-$SUITE_FILTER}"
    ROUNDS="${4:-9}"
    OUT_DIR=docs/bench
    mkdir -p "$OUT_DIR"

    bold "publish: suite=$SUITE reps=$ROUNDS filter=$FILTER"
    # ONE pass, with repetitions and random interleaving.
    #
    # This used to run N separate single-repetition "rounds" first and write a JSON sidecar
    # per round. That was cost without a consumer: nothing read the sidecars, and the paired
    # per-round ratios they were meant to enable are not what bench_main.cpp computes — it
    # reports a median per case. The striation that matters is already here:
    # --benchmark_enable_random_interleaving shuffles the flat list of repetition slots across
    # every filtered case, so a pair's two sides are scattered through the run rather than
    # measured as two consecutive blocks. Keeping a round loop that nothing consumed would
    # have made a full fixarray publish take an hour to produce the same table.
    CHEATAH_BENCH_SUITE="$SUITE" \
    CHEATAH_BENCH_TABLE="$OUT_DIR/$SUITE.md" \
    "$BIN" --benchmark_filter="$FILTER" \
        --benchmark_repetitions="$ROUNDS" --benchmark_min_time=0.3s \
        --benchmark_enable_random_interleaving=true \
        --benchmark_out_format=json --benchmark_out="$OUT_DIR/$SUITE.json" \
        --benchmark_format=console >/dev/null 2>&1 \
        || die "measurement failed"
    bold "wrote $OUT_DIR/$SUITE.md (+ $SUITE.json, the raw Google Benchmark output)"
    ;;

*)
    die "unknown profile '$PROFILE' — expected smoke, gate, or publish"
    ;;
esac
