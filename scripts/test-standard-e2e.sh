#!/usr/bin/env bash
# test-standard-e2e.sh — the Biome Standard, proven end to end from NOTHING.
#
# Starts in an EMPTY temp directory with every cheatah environment variable cleared, and runs
# the real consumer flow a new user would: `biome init` → `biome add` every standard member →
# the generated CMake fetches EVERY member from GitHub by its released tag via CPM (real
# network, no local siblings) → builds → runs a program that exercises each member — the gpu
# probe, a gpu-linalg dot (available()-gated), a plot rendered to a PNG whose bytes are
# verified, and a space.time round trip — and must print `RESULT: PASS`.
#
# Network-dependent BY DESIGN, so it is NOT in the offline pre-push gate: docs/releasing.md
# runs it after the release tags are pushed (the acceptance test that the standard's tagged
# combination actually works together), and .github/workflows/standard-e2e.yml re-proves it
# on demand. The only local input allowed is the biome BINARY under test (built from this
# checkout — the launcher itself is what a release ships).
#
#   scripts/test-standard-e2e.sh [<biome-binary>]     # default: newest build/{release,debug}/bin/biome
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

BIOME="${1:-}"
if [ -z "$BIOME" ]; then
    for c in build/release/bin/biome build/debug/bin/biome; do
        [ -x "$c" ] && { BIOME="$PWD/$c"; break; }
    done
else
    # An explicitly-passed path must be absolutized too. run() cds into the clean-room temp
    # directory, so a RELATIVE path stops resolving the moment it is used — and a relative
    # path is exactly what .github/workflows/standard-e2e.yml passes
    # ("build/release/bin/biome"). That is why standard-e2e failed on EVERY tagged release
    # while passing locally: locally the argument is omitted and the branch above already
    # absolutizes. The binary built fine in CI; it was never found.
    case "$BIOME" in /*) ;; *) BIOME="$PWD/$BIOME" ;; esac
fi
[ -n "$BIOME" ] && [ -x "$BIOME" ] || { echo "[e2e] no biome binary (build the toolchain first)"; exit 2; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
echo "[e2e] empty directory: $W"

# The clean-room environment: no cheatah env vars, no sibling checkouts on any path.
run() ( cd "$W/proj" 2>/dev/null || cd "$W"
        env -u CHEATAH_ROOT -u CHEATAH_LIB_DIR -u CHEATAH_TRUST -u CHEATAH_DIR \
            -u CHEATAH_GPU_DIR -u CHEATAH_MODULE_PATH "$@" )

echo "[e2e] biome init proj…"
run "$BIOME" init proj | sed 's/^/    /'
[ -f "$W/proj/cheatah.toml" ] || { echo "[e2e] FAILED: init produced no manifest"; exit 1; }

echo "[e2e] adding every standard member…"
for ext in cheatah-gpu cheatah-gpu-linalg cheatah-plot cheatah-space; do
    run "$BIOME" add "$ext" | sed 's/^/    /'
    grep -q "$ext" "$W/proj/cheatah.toml" || { echo "[e2e] FAILED: add $ext did not land"; exit 1; }
done

# The program: every member exercised, one verdict.
cat > "$W/proj/src/main.purr" <<'PURR'
# The Biome Standard smoke: one program touching every member of the set.
import io
import ndarray
import linalg
import gpu
import gpulinalg
import plot
import plot.figure as figure
import space.time as st

let fails = 0

# cheatah-gpu: the dispatch math is pure and always answers.
let groups = gpu.dispatch.group_count_1d(1000000, 256)
if groups != 3907 {
    fails = fails + 1
    io.print("FAIL gpu.dispatch:", groups)
}

# cheatah-gpu-linalg: a sum on the device when one exists; the honest skip otherwise.
if gpulinalg.available() {
    let hx = ndarray.array([1.0, 2.0, 3.0, 4.0])
    let dx = gpulinalg.to_device(hx)
    let s = gpulinalg.sum(dx)
    let d = s - 10.0
    if d * d > 0.000001 {
        fails = fails + 1
        io.print("FAIL gpulinalg.sum:", s)
    }
    io.print("gpulinalg: device sum ok")
} else {
    io.print("gpulinalg: no device here — the probe answered honestly; host linalg still works")
    let a = ndarray.array([1.0, 2.0])
    let b = ndarray.array([3.0, 4.0])
    let s = linalg.dot(a, b)
    if s != 11.0 {
        fails = fails + 1
        io.print("FAIL linalg.dot:", s)
    }
}

# cheatah-plot: a figure rendered to a real PNG (the CPU reference path needs no GPU).
let xs = ndarray.array([0.0, 1.0, 2.0, 3.0])
let ys = ndarray.array([0.0, 1.0, 4.0, 9.0])
let fig = figure.line(figure.new_figure(), xs, ys)
fig = figure.title(fig, "the standard, end to end")
fig = figure.size(fig, 320, 240)
plot.save(fig, "standard.png")
let png = io.read_file("standard.png")
if len(png) < 100 {
    fails = fails + 1
    io.print("FAIL plot: png too small")
}
if not ("PNG" in png) {
    fails = fails + 1
    io.print("FAIL plot: no PNG signature")
}
if not ("IEND" in png) {
    fails = fails + 1
    io.print("FAIL plot: no IEND chunk")
}

# cheatah-space: the J2000 Julian-date round trip (well-known constants).
let jd = st.unix_to_jd(946728000.0)
let dj = jd - 2451545.0
if dj * dj > 0.0000000001 {
    fails = fails + 1
    io.print("FAIL space.time unix->jd:", jd)
}
let back = st.jd_to_unix(jd)
let db = back - 946728000.0
if db * db > 0.000001 {
    fails = fails + 1
    io.print("FAIL space.time jd->unix:", back)
}

if fails == 0 {
    io.print("RESULT: PASS")
} else {
    io.print("RESULT: FAIL (", fails, ")")
}
PURR

echo "[e2e] biome configure (CPM fetches every member from GitHub by tag — real network)…"
run "$BIOME" configure | sed 's/^/    /' | tail -5
echo "[e2e] biome build…"
run "$BIOME" build | sed 's/^/    /' | tail -5

# The runtime that biome's build fetched: find the cheatah binary inside the project build tree.
RUNTIME="$(find "$W/proj/build" -type f -name cheatah -perm -u+x 2>/dev/null | head -1)"
APP="$(find "$W/proj/build" -type f -name 'proj.so' 2>/dev/null | head -1)"
[ -n "$RUNTIME" ] || { echo "[e2e] FAILED: the fetched toolchain has no cheatah runtime"; exit 1; }
[ -n "$APP" ] || { echo "[e2e] FAILED: the build produced no program module"; exit 1; }

echo "[e2e] running the standard smoke…"
out="$(cd "$W/proj" && "$RUNTIME" "$APP" 2>&1)"
echo "$out" | sed 's/^/    /'
echo "$out" | grep -q "RESULT: PASS" || { echo "[e2e] FAILED: the program did not pass"; exit 1; }
[ -f "$W/proj/standard.png" ] || { echo "[e2e] FAILED: no PNG on disk"; exit 1; }

echo "[e2e] PASS — empty directory → biome → GitHub tags → build → a rendered plot. The standard holds."
