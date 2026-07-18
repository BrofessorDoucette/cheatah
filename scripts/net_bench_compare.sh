#!/usr/bin/env bash
# net_bench_compare.sh — measure cheatah's HTTPS download speed AGAINST known-fast reference clients
# (curl, wget) on the IDENTICAL URL, interleaved and repeated, so a throttled/variable network
# cancels out in the RATIO. Absolute MB/s is meaningless under throttling; the cheatah/curl ratio is
# the honest metric — it isolates cheatah's own efficiency. (Owner methodology, 2026-07-18.)
#
#   scripts/net_bench_compare.sh <https-url> [rounds] [max_mib]
#
# Prints per-round MB/s for curl / wget / cheatah, then medians and the cheatah/curl ratio.
set -uo pipefail

URL="${1:?usage: net_bench_compare.sh <https-url> [rounds] [max_mib]}"
ROUNDS="${2:-5}"
MAX_MIB="${3:-256}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PURRC="$ROOT/build/release/bin/purrc"
RUNTIME="$ROOT/build/release/bin/cheatah"
LIB="$ROOT/build/release/lib"
SO="$ROOT/build/release/bin/net_get.so"

# Build the minimal cheatah GET client once against this checkout's crypto stdlib.
if [ ! -x "$RUNTIME" ] || [ ! -x "$PURRC" ]; then
    echo "net_bench: build the release tree first: cmake --build $ROOT/build/release" >&2; exit 1
fi
if [ ! -f "$SO" ] || [ "$ROOT/tests/benchmarks/net_get.purr" -nt "$SO" ]; then
    LINKS=(--link -ldl)
    for a in tls socket string hashlib parsers x25519 aead ed25519 p256 p384 io sys time; do
        LINKS+=(--link "$LIB/libcheatah_$a.a")
    done
    "$PURRC" "$ROOT/tests/benchmarks/net_get.purr" -o "$SO" \
        --cxxflag "-I$ROOT/stdlib" "${LINKS[@]}" >&2 || { echo "net_bench: build failed" >&2; exit 1; }
fi

# One timed download; echoes MB/s (or "fail"). $1 = client name.
run_curl() { curl -s --max-time 300 -o /dev/null -w '%{speed_download}' "$URL" 2>/dev/null \
             | awk '{printf "%.4f", $1/1048576}'; }
run_wget() {
    local t; t=$( { /usr/bin/time -f '%e' wget -q -O /dev/null "$URL"; } 2>&1 | tail -1 )
    local sz; sz=$(curl -s --max-time 300 -o /dev/null -w '%{size_download}' "$URL" 2>/dev/null)
    awk -v s="$sz" -v t="$t" 'BEGIN{ if (t>0) printf "%.4f", (s/1048576)/t; else print "fail" }'
}
run_cheatah() {
    "$RUNTIME" "$SO" "$URL" "$MAX_MIB" 2>/dev/null | awk '/^NET_GET/ {
        for (i=1;i<=NF;i++){ if ($i ~ /^mbps=/){ split($i,a,"="); printf "%.4f", a[2] } } }'
}

median() { printf '%s\n' "$@" | grep -vi fail | sort -n | awk '{v[NR]=$1} END{ if(NR==0){print "fail"} else print v[int((NR+1)/2)] }'; }

echo "net_bench: URL=$URL  rounds=$ROUNDS  (throttle-immune: compare the RATIO, not absolute MB/s)"
printf '%-6s %12s %12s %12s\n' "round" "curl" "wget" "cheatah"
c_all=(); w_all=(); h_all=()
for r in $(seq 1 "$ROUNDS"); do
    # Interleave so all three clients see the same instantaneous network within a round.
    c=$(run_curl); w=$(run_wget); h=$(run_cheatah)
    c_all+=("$c"); w_all+=("$w"); h_all+=("$h")
    printf '%-6s %12s %12s %12s\n' "$r" "$c" "$w" "$h"
done
cm=$(median "${c_all[@]}"); wm=$(median "${w_all[@]}"); hm=$(median "${h_all[@]}")
printf '%-6s %12s %12s %12s   (MB/s median)\n' "med" "$cm" "$wm" "$hm"
awk -v h="$hm" -v c="$cm" 'BEGIN{ if (c>0 && h!="fail") printf "net_bench: cheatah/curl ratio = %.2f  (1.0 = parity; the throttle-immune verdict)\n", h/c; else print "net_bench: ratio unavailable" }'
