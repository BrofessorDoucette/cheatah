#!/usr/bin/env bash
# Static analysis: clang-tidy across the repo's C++ — THE canonical driver, like
# scripts/cppcheck.sh. Part of the QA gate; also runnable on its own:
#
#     bash scripts/clang_tidy.sh          # main pass: every first-party TU in the compile DB
#     bash scripts/clang_tidy.sh --gen    # generated-code lane: the gen/*.gen.cpp fixtures
#
# Other repos' gates call THIS script (directly or via deploy's qa_stage_tidy); the only
# sanctioned duplicate of its mechanics is cheatah-gpu-linalg's, which must stay
# self-contained in its public tree. All POLICY lives in the repo's committed .clang-tidy
# (a verbatim copy of cheatah/.clang-tidy — byte-compared below, never forked).
#
# Knobs (env, all with derived defaults):
#   TIDY_BUILD_DIR   dir holding compile_commands.json (default: first of build/release,
#                    build/debug, build/gate, build that has one)
#   TIDY_SOURCE_RE   regex of first-party TUs, matched against compile-DB entries and
#                    passed to run-clang-tidy. An ALLOWLIST — this is what keeps
#                    build/_deps and third_party/ out (clang-tidy 18 has no
#                    exclude-header-filter, so allowlist is the only correct shape).
#                    Default: /(<repo-dir-name>|src|include|tests)/
#   TIDY_HEADER_RE   -header-filter regex (default: TIDY_SOURCE_RE)
#   TIDY_WERROR      the per-repo burn-down ratchet. The EFFECTIVE -warnings-as-errors is
#                    always "cert-*,$TIDY_WERROR" — the cert floor is prepended
#                    unconditionally; a repo cannot opt out of cert being fatal.
#   TIDY_JOBS        parallelism (default: nproc)
#
# Fails closed: missing clang-tidy, missing compile DB, a source regex that matches zero
# DB entries ("nothing would be checked" — the header-only-library lesson, generalized),
# a .clang-tidy that has drifted from canonical, or a canary run that does NOT fail.
#
# The CANARY (house rule: verify tests can fail): before the real pass, a scratch TU with
# a known cert-env33-c violation (std::system) is linted under this repo's .clang-tidy —
# if that does not exit nonzero, the config or tool is broken and the stage fails. <1s.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"
REPO="$(basename "$ROOT")"

command -v clang-tidy >/dev/null 2>&1 || {
    echo "[clang-tidy] clang-tidy not found — install it (e.g. 'sudo apt install clang-tidy')."
    exit 1
}
command -v run-clang-tidy >/dev/null 2>&1 || {
    echo "[clang-tidy] run-clang-tidy not found — it ships with clang-tidy (clang-tools)."
    exit 1
}
[ -f "$ROOT/.clang-tidy" ] || { echo "[clang-tidy] $REPO has no .clang-tidy — copy the canonical one from cheatah/."; exit 1; }

# Anti-fork: the repo's .clang-tidy must be byte-identical to the canonical copy whenever
# the canonical checkout is reachable (it always is on the workstation; a standalone clone
# of a public repo simply skips this and relies on deploy/scripts/tidy_fleet.sh).
CANON="${CHEATAH_DIR:-$ROOT/../cheatah}/.clang-tidy"
if [ "$REPO" != "cheatah" ] && [ -f "$CANON" ] && ! cmp -s "$ROOT/.clang-tidy" "$CANON"; then
    echo "[clang-tidy] $REPO/.clang-tidy has DRIFTED from the canonical $CANON — re-copy it; policy is edited there, never per repo."
    exit 1
fi

WERROR="cert-*${TIDY_WERROR:+,$TIDY_WERROR}"
JOBS="${TIDY_JOBS:-$(nproc)}"

# ---- canary: prove this config CAN fail, on every run -----------------------------
CANARY_DIR="$(mktemp -d)"
trap 'rm -rf "$CANARY_DIR"' EXIT
cat > "$CANARY_DIR/canary.cpp" <<'EOF'
#include <cstdlib>
int main() { return std::system("ls"); }
EOF
if clang-tidy --quiet --config-file="$ROOT/.clang-tidy" --warnings-as-errors="$WERROR" \
        "$CANARY_DIR/canary.cpp" -- -std=c++20 >/dev/null 2>&1; then
    echo "[clang-tidy] CANARY PASSED CLEAN — a known cert-env33-c violation was not flagged fatal."
    echo "[clang-tidy] The config or tool is broken; refusing to certify anything."
    exit 1
fi

# ---- generated-code lane (--gen) --------------------------------------------------
if [ "${1:-}" = "--gen" ]; then
    [ -d gen ] || { echo "[clang-tidy] --gen: no gen/ directory in $REPO."; exit 1; }
    # Regenerate first: the fixtures must reflect the CURRENT codegen before we lint them.
    bash gen/generate.sh >/dev/null || { echo "[clang-tidy] --gen: gen/generate.sh failed."; exit 1; }
    if ! git diff --exit-code --stat -- gen/ ; then
        echo "[clang-tidy] Generated fixtures are STALE — the codegen changed. Review the diff"
        echo "[clang-tidy] above (it is the codegen change made visible), commit gen/*.gen.cpp."
        exit 1
    fi
    GEN_FILES=(gen/*.gen.cpp)
    [ -e "${GEN_FILES[0]}" ] || { echo "[clang-tidy] --gen: no gen/*.gen.cpp fixtures."; exit 1; }
    # The fixtures are not in any compile DB; hand clang-tidy the same include roots
    # purrc's emitted C++ expects (every stdlib module dir). gen/.clang-tidy is picked up
    # by directory walk and scopes out generator-legitimate cosmetics.
    INC=(); for d in stdlib/*/; do INC+=("-I$d"); done
    LOG=/tmp/${REPO}_clang_tidy_gen.log
    clang-tidy --quiet --warnings-as-errors="$WERROR" "${GEN_FILES[@]}" -- -std=c++20 "${INC[@]}" \
        >"$LOG" 2>&1
    RC=$?
    ERRS=$(grep ' error: ' "$LOG" | sort -u | grep -c . || true)
    WARNS=$(grep ' warning: ' "$LOG" | sort -u | grep -c . || true)
    echo "[clang-tidy] gen-lane: WErrors=$WERROR errors=$ERRS warnings-outstanding=$WARNS"
    if [ "$RC" -ne 0 ]; then
        grep -E ' (error|warning): ' "$LOG" | sort -u | head -40
        echo "[clang-tidy] findings in GENERATED code — never hand-edit a .gen.cpp: fix the"
        echo "[clang-tidy] .purr source or compiler/codegen.cpp, then 'bash gen/generate.sh'."
        exit 1
    fi
    echo "[clang-tidy] gen-lane clean at the current ratchet (full log: $LOG)."
    exit 0
fi

# ---- main pass: first-party TUs from the compile DB -------------------------------
BUILD_DIR=""
for d in "${TIDY_BUILD_DIR:-}" build/release build/debug build/gate build; do
    [ -n "$d" ] && [ -f "$d/compile_commands.json" ] && { BUILD_DIR="$d"; break; }
done
[ -n "$BUILD_DIR" ] || {
    echo "[clang-tidy] no compile_commands.json under ${TIDY_BUILD_DIR:-build/{release,debug,gate}} —"
    echo "[clang-tidy] configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first."
    exit 1
}
DB="$BUILD_DIR/compile_commands.json"

SOURCE_RE="${TIDY_SOURCE_RE:-/(${REPO}|src|include|tests)/}"
HEADER_RE="${TIDY_HEADER_RE:-$SOURCE_RE}"

# Anchor the allowlists at the REPO ROOT. Unanchored, a pattern like /(atomizer|tests)/
# matches every path in the repo, because the repo DIRECTORY itself is named atomizer —
# /…/Dev/atomizer/third_party/x.hpp contains "/atomizer/" — silently re-admitting
# third_party/ and build/_deps/. A pattern that already starts with ^ is used verbatim.
case "$SOURCE_RE" in ^*) : ;; *) SOURCE_RE="^$ROOT$SOURCE_RE" ;; esac
case "$HEADER_RE" in ^*) : ;; *) HEADER_RE="^$ROOT$HEADER_RE" ;; esac

# Zero matches would mean a green stage that checked nothing — fail instead.
MATCHES=$(grep -o '"file": *"[^"]*"' "$DB" | sed 's/.*"file": *"//;s/"$//' | grep -Ec "$SOURCE_RE" || true)
if [ "${MATCHES:-0}" -eq 0 ]; then
    echo "[clang-tidy] TIDY_SOURCE_RE '$SOURCE_RE' matches ZERO entries in $DB — nothing would be checked."
    exit 1
fi

LOG=/tmp/${REPO}_clang_tidy.log
echo "[clang-tidy] $MATCHES TU(s) from $DB — WErrors=$WERROR, -j$JOBS…"
run-clang-tidy -p "$BUILD_DIR" -quiet -j "$JOBS" \
    -header-filter "$HEADER_RE" -warnings-as-errors "$WERROR" "$SOURCE_RE" \
    >"$LOG" 2>&1
RC=$?
ERRS=$(grep ' error: ' "$LOG" | sort -u | grep -c . || true)
WARNS=$(grep ' warning: ' "$LOG" | sort -u | grep -c . || true)
echo "[clang-tidy] WErrors=$WERROR errors=$ERRS warnings-outstanding=$WARNS"
if [ "$RC" -ne 0 ]; then
    grep ' error: ' "$LOG" | sort -u | head -40
    echo "[clang-tidy] fatal findings above (full log: $LOG) — fix them, or annotate an"
    echo "[clang-tidy] intentional exception with NOLINT(check-name) + a same-line reason."
    exit 1
fi
echo "[clang-tidy] clean at the current ratchet (full log: $LOG)."
