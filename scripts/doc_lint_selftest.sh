#!/usr/bin/env bash
# doc_lint_selftest.sh — prove the documentation-truth lint can actually FAIL.
#
# scripts/doc_lint.purr reported OK for months while silently skipping 103 of its inputs: it
# only recognised @test when the tag began a stripped comment line, and cheatah's house style
# puts @complexity and @alloc on that line first. 31 dead test references hid in the gap. A lint
# with no test of its own cannot tell you it has gone blind, so this plants known defects in a
# throwaway tree and asserts the lint names every one of them — and stays quiet about the valid
# tag, which is what separates "catches everything" from "reports everything".
#
#   bash scripts/doc_lint_selftest.sh
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

PURRC=build/release/bin/purrc
CHEATAH=build/release/bin/cheatah
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    PURRC=build/debug/bin/purrc
    CHEATAH=build/debug/bin/cheatah
fi
if [ ! -x "$PURRC" ] || [ ! -x "$CHEATAH" ]; then
    echo "[doc-lint-selftest] SKIP — no purrc/cheatah build (need build/release or build/debug)"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SO="$TMP/doc_lint.so"
"$PURRC" scripts/doc_lint.purr -o "$SO" >/dev/null \
    || { echo "[doc-lint-selftest] FAILED: purrc scripts/doc_lint.purr"; exit 1; }

# A miniature repo: the lint's `tests` mode walks stdlib/ for tags and the test dirs for TEST().
FX="$TMP/fixture"
mkdir -p "$FX/stdlib/requests" "$FX/stdlib/tests"
: > "$FX/stdlib/requests/requests.purr"   # named explicitly by check_tests; must exist
cat > "$FX/stdlib/tests/real_test.cpp" <<'CPP'
TEST(RealSuite, RealCase) {}
CPP
cat > "$FX/stdlib/probe.hpp" <<'HPP'
/// The tag LEADS the line — the one arrangement the pre-fix lint already caught.
/// @test Ghost.Leading
void a();

/// The tag TRAILS @complexity/@alloc on one line — the house style, and the blind spot.
/// @complexity O(1)  @alloc none.  @test Ghost.Trailing
void b();

// A plain `//` comment, the form the JSON internals use.
// @complexity O(1)  @alloc none.  @test Ghost.SlashSlash
void c();

// A `//` tag whose name list WRAPS onto a continuation line.
// @complexity O(1)  @alloc none.  @test Ghost.WrapHead,
// Ghost.WrapTail
void d();

/// A tag that resolves: the lint must stay silent about this one.
/// @complexity O(1)  @alloc none.  @test RealSuite.RealCase
void e();
HPP

OUT="$(cd "$FX" && "$OLDPWD/$CHEATAH" "$SO" tests 2>&1)"
STATUS=$?

fail=0
for name in Ghost.Leading Ghost.Trailing Ghost.SlashSlash Ghost.WrapHead Ghost.WrapTail; do
    if ! grep -q "$name" <<<"$OUT"; then
        echo "[doc-lint-selftest] MISS — the lint did not report the planted dead tag $name"
        fail=1
    fi
done
if grep -q "RealSuite.RealCase" <<<"$OUT"; then
    echo "[doc-lint-selftest] FALSE POSITIVE — the lint reported the VALID tag RealSuite.RealCase"
    fail=1
fi
if [ "$STATUS" -eq 0 ]; then
    echo "[doc-lint-selftest] FAILED — the lint exited 0 on a tree full of dead tags"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "--- lint output ---"; echo "$OUT"
    exit 1
fi
# ---- the two test-directory lists must agree -----------------------------------------------
# scripts/doc_lint.purr decides whether a @test name is VALID; docs/gen-cheatah/gen.purr decides
# whether the site can LINK it. They were separate literals and had already drifted — one grew
# tests/previously_broken and the other did not — so a name could be accepted by the lint and
# still render as dead text. Compare them mechanically rather than by memory.
list_of() { sed -n "/^fn test_dirs()/,/^}/p" "$1" | tr -d ' \n' | grep -o '\[.*\]'; }
L_LINT="$(list_of scripts/doc_lint.purr)"
L_GEN="$(list_of docs/gen-cheatah/gen.purr)"
if [ -z "$L_LINT" ] || [ -z "$L_GEN" ]; then
    echo "[doc-lint-selftest] FAILED — could not read test_dirs() from both programs"
    echo "  doc_lint.purr: ${L_LINT:-<none>}"
    echo "  gen.purr:      ${L_GEN:-<none>}"
    exit 1
fi
if [ "$L_LINT" != "$L_GEN" ]; then
    echo "[doc-lint-selftest] DRIFT — the two test-directory lists disagree:"
    echo "  scripts/doc_lint.purr     $L_LINT"
    echo "  docs/gen-cheatah/gen.purr $L_GEN"
    exit 1
fi

echo "[doc-lint-selftest] OK — 5 planted dead tags all reported, the valid tag was not"
echo "[doc-lint-selftest] OK — doc_lint.purr and gen.purr agree on $L_LINT"
