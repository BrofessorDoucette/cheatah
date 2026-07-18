#!/usr/bin/env bash
# tls_loopback_bench.sh — the THROTTLE-FREE proof. Over 127.0.0.1 (no WAN, no throttle) a real
# `openssl s_server` serves a large file; we download it with curl (the known-fast reference) and
# with cheatah's own TLS client, and report the cheatah/curl ratio. Loopback has ~GB/s headroom, so
# this is where the socket-tuning + multi-record-drain + copy fixes actually show — and where "how
# close is cheatah to a known-fast TLS stack's ceiling" is answered honestly. (Owner methodology.)
#
#   scripts/tls_loopback_bench.sh [payload_mib] [rounds]
set -uo pipefail

MIB="${1:-64}"
ROUNDS="${2:-5}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="$ROOT/build/release/bin/cheatah"
SO="$ROOT/build/release/bin/net_get.so"
PORT=$(( (RANDOM % 20000) + 40000 ))
TMP="$(mktemp -d /tmp/tls-loopback.XXXXXX)"
cleanup() { pkill -f "s_server -accept $PORT" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT

command -v openssl >/dev/null || { echo "tls_loopback_bench: openssl CLI required" >&2; exit 1; }
[ -x "$RUNTIME" ] || { echo "build the release tree first" >&2; exit 1; }
if [ ! -f "$SO" ]; then
    echo "tls_loopback_bench: build net_get first — run scripts/net_bench_compare.sh once" >&2; exit 1
fi

# A large incompressible payload (so the transfer is genuinely bandwidth/CPU-bound, not gzip'd away)
# and a throwaway localhost cert.
head -c "$((MIB * 1024 * 1024))" /dev/urandom > "$TMP/payload.bin"
openssl req -x509 -newkey ed25519 -keyout "$TMP/key.pem" -out "$TMP/cert.pem" \
    -days 1 -nodes -subj /CN=localhost -addext subjectAltName=DNS:localhost 2>/dev/null

# openssl s_server -WWW emulates a file server (serves files from cwd over TLS 1.3).
( cd "$TMP" && openssl s_server -accept "$PORT" -cert cert.pem -key key.pem -tls1_3 -WWW \
    >/dev/null 2>&1 & )
sleep 1

URL="https://localhost:$PORT/payload.bin"
WANT_SHA=$(sha256sum "$TMP/payload.bin" | cut -d' ' -f1)
run_curl()    { curl -s --cacert "$TMP/cert.pem" --max-time 120 -o /dev/null \
                -w '%{speed_download}' "$URL" 2>/dev/null | awk '{printf "%.2f", $1/1048576}'; }
run_cheatah() { SSL_CERT_FILE="$TMP/cert.pem" "$RUNTIME" "$SO" "$URL" "$((MIB + 8))" "$TMP/got.bin" 2>/dev/null \
                | awk '/^NET_GET/{for(i=1;i<=NF;i++){if($i~/^mbps=/){split($i,a,"=");printf "%.2f",a[2]}}}'; }

# Byte-identity through the REAL TLS drain path: cheatah's downloaded bytes must sha256-match the
# payload. This is the integration guard for the record-framing + multi-record-drain + copy changes.
SSL_CERT_FILE="$TMP/cert.pem" "$RUNTIME" "$SO" "$URL" "$((MIB + 8))" "$TMP/got.bin" >/dev/null 2>&1
GOT_SHA=$(sha256sum "$TMP/got.bin" 2>/dev/null | cut -d' ' -f1)
if [ "$GOT_SHA" = "$WANT_SHA" ]; then
    echo "tls_loopback_bench: INTEGRITY OK — ${MIB} MiB downloaded byte-identical through cheatah TLS"
else
    echo "tls_loopback_bench: INTEGRITY FAIL — cheatah download did not match (want $WANT_SHA got $GOT_SHA)" >&2
    exit 1
fi
median() { printf '%s\n' "$@" | grep -vi fail | sort -n | awk '{v[NR]=$1} END{if(NR)print v[int((NR+1)/2)];else print "fail"}'; }

echo "tls_loopback_bench: ${MIB} MiB over 127.0.0.1 vs openssl s_server  (throttle-free ceiling)"
printf '%-6s %12s %12s\n' "round" "curl" "cheatah"
c_all=(); h_all=()
for r in $(seq 1 "$ROUNDS"); do
    c=$(run_curl); h=$(run_cheatah)
    c_all+=("$c"); h_all+=("$h")
    printf '%-6s %12s %12s\n' "$r" "$c" "$h"
done
cm=$(median "${c_all[@]}"); hm=$(median "${h_all[@]}")
printf '%-6s %12s %12s   (MB/s median)\n' "med" "$cm" "$hm"
awk -v h="$hm" -v c="$cm" 'BEGIN{ if(c>0&&h!="fail") printf "tls_loopback_bench: cheatah/curl ratio = %.2f  (loopback ceiling; both hit a real openssl peer)\n", h/c; else print "ratio unavailable" }'
