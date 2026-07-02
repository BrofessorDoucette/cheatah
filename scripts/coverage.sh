#!/usr/bin/env bash
# Measure cheatah standard-library test coverage with clang source-based coverage.
#
# Coverage is measured across BOTH in-process test binaries:
#   * cheatah_tests       — the unit tests (crypto vectors, socket/io loopback, the
#                           linalg core + smoke, ndarray, string, math, …).
#   * cheatah_purrc_tests — the system tests, filtered to the ones that drive a module's
#                           C++ API IN-PROCESS. The `tls`/`websocket` handshake+record
#                           paths can only run against a real peer (openssl s_server), so
#                           TlsSys.* exercises tls.cpp/p256.cpp/rsa_verify.hpp directly in
#                           this process; without it those modules are unreachable from a
#                           pure unit test (cheatah_tests links no OpenSSL). The subprocess
#                           e2e tests do NOT add in-process coverage and are skipped here.
# Both profiles are merged, so the 100% line+function gate covers the crypto/network stack
# too. This means the coverage stage needs `openssl` on PATH (as the TLS test peer).
#
#   scripts/coverage.sh               # per-file summary report for stdlib/
#   scripts/coverage.sh show <file>   # uncovered lines of one file, e.g. stdlib/linalg/routines.cpp
#   scripts/coverage.sh funcs <file>  # per-function coverage of one file
#   scripts/coverage.sh update-readme # rewrite the coverage table in README.md
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

# The subset of cheatah_purrc_tests that runs a module's C++ API in this process (so its
# lines land in the coverage profile). Add future in-process system suites here.
SYS_COVERAGE_FILTER="${SYS_COVERAGE_FILTER:-TlsSys.*:WebSocketSys.*}"

B=build/cov
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCHEATAH_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" >/tmp/cheatah_cov_cfg.log 2>&1 \
  || { tail -15 /tmp/cheatah_cov_cfg.log; exit 1; }
cmake --build "$B" --target cheatah_tests cheatah_purrc_tests >/tmp/cheatah_cov_build.log 2>&1 \
  || { tail -25 /tmp/cheatah_cov_build.log; exit 1; }

# The WebSocketSys.* system tests (part of $SYS_COVERAGE_FILTER) need a real Node `ws` echo
# server as their peer — install it (lockfile-pinned) if it isn't present yet.
if [ ! -d tests/fixtures/node_modules/ws ] && command -v npm >/dev/null 2>&1; then
  ( cd tests/fixtures && (npm ci >/dev/null 2>&1 || npm install >/dev/null 2>&1) ) || true
fi

( cd "$B"
  rm -f ./*.profraw
  LLVM_PROFILE_FILE=unit.profraw ./bin/cheatah_tests >/dev/null 2>&1
  # The in-process system tests each spawn helper server processes (openssl s_server, a Node
  # `ws` server). Run EACH such test in its OWN cheatah_purrc_tests process (one server at a
  # time) — spawning many back-to-back in a single process is fragile on constrained hosts.
  # Each run writes its own profraw; test PASS/FAIL is gated elsewhere, so ignore exit status.
  mapfile -t _systests < <(./bin/cheatah_purrc_tests --gtest_filter="$SYS_COVERAGE_FILTER" \
      --gtest_list_tests 2>/dev/null | awk '/\.$/{s=$1} /^  /{gsub(/ /,"");print s $0}')
  _i=0
  for _t in "${_systests[@]}"; do
    LLVM_PROFILE_FILE="sys-$_i.profraw" ./bin/cheatah_purrc_tests --gtest_filter="$_t" \
      >/dev/null 2>&1 || true
    _i=$((_i + 1))
  done
  llvm-profdata merge -sparse unit.profraw sys-*.profraw -o merged.profdata )

OBJS=(./"$B"/bin/cheatah_tests -object ./"$B"/bin/cheatah_purrc_tests)
PROF="-instr-profile=$B/merged.profdata"
SRCS=$(git ls-files 'stdlib/*.cpp' 'stdlib/*.hpp' | grep -v '/tests/')

case "${1:-report}" in
    show)  llvm-cov show   "${OBJS[@]}" $PROF "${2:?usage: coverage.sh show <file>}" 2>/dev/null \
             | grep -nE '\|[[:space:]]*0\|' || echo "all lines covered in ${2}" ;;
    funcs) llvm-cov report "${OBJS[@]}" $PROF -show-functions "${2:?usage: coverage.sh funcs <file>}" 2>/dev/null ;;
    update-readme)
        # Functions / regions / branches come from llvm-cov's per-file report TOTAL.
        # Columns: Regions MissedRegions RCover Functions MissedFuncs FExec Lines
        # MissedLines LCover Branches MissedBranches BCover.
        total=$(llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null | awk '$1=="TOTAL"{$1="";print}')
        read -r regions mreg rcov funcs mfun fexec lines mlin lcov branches mbr bcov <<<"$total"
        # LINE coverage is computed from the MERGED execution view (llvm-cov export
        # segments) rather than the report summary, counting each REGION-ENTRY line once.
        # Two reasons the report summary is wrong for this codebase:
        #   1. Templated headers: the summary expands a source line per-instantiation and
        #      miscounts it "missed" when one instantiation of an error/edge path isn't
        #      hit — even though the line IS executed (merging across instantiations fixes it).
        #   2. Structural lines (e.g. the `}` closing a catch whose body always `return`s)
        #      are unreachable; counting only region ENTRIES ignores that noise.
        # A line is covered iff some instantiation runs the region that starts on it, so a
        # genuinely-untested statement still fails (its region entry has count 0).
        read -r lcn lt lcov < <(llvm-cov export "${OBJS[@]}" $PROF $SRCS 2>/dev/null | python3 -c '
import json, sys
from collections import defaultdict
d = json.load(sys.stdin)
cov = tot = 0
for f in d["data"][0]["files"]:
    name = f["filename"]
    if "/stdlib/" not in name or "/tests/" in name:
        continue
    # Lines tagged `LCOV_EXCL_LINE` are genuinely-unreachable DEFENSIVE branches — e.g. a
    # crypto refusal path a conformant TLS peer cannot trigger, which we deliberately do NOT
    # reach by mirroring a malformed server. Exclude them from the denominator. Use sparingly:
    # each marker must carry a justification comment right on the line (grep the sources to audit).
    excl = set()
    try:
        with open(name) as fh:
            for i, ln in enumerate(fh, 1):
                if "LCOV_EXCL_LINE" in ln:
                    excl.add(i)
    except OSError:
        pass
    mx = defaultdict(int); seen = set()
    for s in f["segments"]:
        line, count, hasCount, isEntry = s[0], s[2], s[3], s[4]
        if hasCount and isEntry and line not in excl:   # region-entry lines, merged across instantiations
            seen.add(line); mx[line] = max(mx[line], count)
    tot += len(seen); cov += sum(1 for l in seen if mx[l] > 0)
pct = ("%.2f%%" % (100.0 * cov / tot)) if tot else "100.00%"
print(cov, tot, pct)
')
        python3 - "$lcov" "$lcn" "$lt" "$fexec" "$((funcs - mfun))" "$funcs" "$rcov" "$bcov" <<'PY'
import re, sys
lcov, lcn, lt, fexec, fcn, ft, rcov, bcov = sys.argv[1:9]
table = (
    "<!-- coverage:start -->\n"
    "| Metric | Standard library |\n"
    "|--------|------------------|\n"
    f"| **Lines** | {lcov} ({lcn}/{lt}) |\n"
    f"| **Functions** | {fexec} ({fcn}/{ft}) |\n"
    f"| Regions | {rcov} |\n"
    f"| Branches | {bcov} |\n"
    "<!-- coverage:end -->"
)
src = open("README.md").read()
out, n = re.subn(r"<!-- coverage:start -->.*?<!-- coverage:end -->", lambda _: table, src, flags=re.S)
if n != 1:
    sys.stderr.write("coverage markers not found in README.md\n"); sys.exit(1)
open("README.md", "w").write(out)
print(f"README coverage table: lines {lcov} ({lcn}/{lt}), functions {fexec} ({fcn}/{ft})")
PY
        ;;
    *)     llvm-cov report "${OBJS[@]}" $PROF $SRCS 2>/dev/null ;;
esac
