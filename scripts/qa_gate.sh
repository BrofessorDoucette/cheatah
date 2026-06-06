#!/usr/bin/env bash
# QA gate for cheatah — the quality-control checks that must pass before a push.
#
# Invoked by the git pre-push hook (.githooks/pre-push) for every push to any
# remote/branch, and runnable by hand. Exits non-zero to BLOCK the push.
#
#   1. Configure (debug for tests, release for benchmarks).
#   2. Build (debug).
#   3. Unit test suite (hard gate) — ctest.
#   4. Benchmarks: build optimized + run a smoke pass (hard gate that they
#      build & run; perf-regression gating comes once we archive history).
#
# This is intentionally lean for the scaffolding stage. As the language grows we
# layer on more rigor (benchmark regression vs archived history, etc.).
#
# Env:  QA_GATE_SKIP=1            bypass the gate entirely (discouraged)
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

# 1. Configure ---------------------------------------------------------------
bold "Configuring (debug + release)…"
cmake --preset debug   >/tmp/cheatah_cfg_debug.log   2>&1 || { tail -20 /tmp/cheatah_cfg_debug.log;   fail "configure (debug)"; }
cmake --preset release >/tmp/cheatah_cfg_release.log 2>&1 || { tail -20 /tmp/cheatah_cfg_release.log; fail "configure (release)"; }

# 2. Build (debug) -----------------------------------------------------------
bold "Building (debug)…"
cmake --build --preset debug >/tmp/cheatah_build_debug.log 2>&1 || { tail -30 /tmp/cheatah_build_debug.log; fail "debug build"; }

# 3. Unit tests (hard gate) --------------------------------------------------
bold "Running unit test suite…"
ctest --preset debug --output-on-failure || fail "unit tests"

# 4. Benchmarks: build optimized + smoke run (hard gate) ---------------------
bold "Building benchmarks (release)…"
cmake --build --preset release-benchmarks >/tmp/cheatah_build_bench.log 2>&1 || { tail -30 /tmp/cheatah_build_bench.log; fail "release benchmark build"; }

bold "Running benchmarks (smoke pass, min-time ${MIN_TIME})…"
./build/release/bin/cheatah_benchmarks --benchmark_min_time="${MIN_TIME}" || fail "benchmark run"

bold "QA gate PASSED — push may proceed."
exit 0
