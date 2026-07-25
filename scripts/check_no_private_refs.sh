#!/usr/bin/env bash
# Private-reference scan — keeps sibling-project names out of the PUBLIC tree.
#
# cheatah is public; several sibling projects are not. A stray "as <project> does"
# in a comment is harmless to the build and permanent in a published repo, so this
# runs in the QA gate: catch it before the push, every time, instead of relying on
# anyone remembering. (Commit messages are NOT rewritable after the fact — an
# already-pushed reference stays published — which is exactly why the check is
# pre-push.)
#
# Scope: tracked files, including docs/html (the generated site embeds test sources
# verbatim in its source-view pages, so a scrubbed source with a stale site still leaks).
#
# "BigBrain LLC" in copyright headers is the company and is LEGITIMATE — allowlisted.
#
# Deliberately NOT in the pattern: "element" and "ash" (array elements, hash/bash/flash
# — the false-positive rate makes the check useless). Word boundaries are mandatory:
# "scribe" matches "describe", "conjure" can appear as an ordinary verb.
#
# If a hit is a genuine false positive (e.g. a real cryptographic Alice/Bob exposition),
# rephrase it rather than weakening this check — the names are cheap to avoid.
#
# Modes:
#   check_no_private_refs.sh              scan the tracked tree (the QA-gate default)
#   check_no_private_refs.sh --message F  scan one commit-message file (commit-msg hook)
#   check_no_private_refs.sh --range A..B scan commit messages in a range (pre-push)
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

PATTERN='bigbrain|atomizer|godspeed|sherlock|conjure|looking-glass|lookingglass|scribe|zebra|glgan|alice'
ALLOW='BigBrain LLC|Copyright \(c\) [0-9]+ BigBrain'

report() {  # report <what> <hits>
    local what="$1" hits="$2"
    local n; n="$(printf '%s\n' "$hits" | wc -l | tr -d ' ')"
    echo "private-refs: FAIL — $n reference(s) to non-public sibling projects in $what:"
    printf '%s\n' "$hits" | head -40
    echo
    echo "private-refs: rephrase these — keep the technical meaning, drop the project name."
    echo "              (Source: fix and regenerate docs/html if a source-view page is among"
    echo "               them. Commit message: 'git commit --amend'. A PUSHED message cannot"
    echo "               be fixed without rewriting history and invalidating every release tag,"
    echo "               which is why this check exists.)"
}

case "${1:-}" in
--message)
    file="${2:?--message needs a commit-message file}"
    # Ignore comment lines git strips from the final message.
    hits="$(grep -nIwiE "$PATTERN" "$file" 2>/dev/null | grep -v '^[0-9]*:#' | grep -viE "$ALLOW" || true)"
    [ -z "$hits" ] || { report "this commit message" "$hits"; exit 1; }
    exit 0
    ;;
--range)
    range="${2:?--range needs A..B}"
    hits=""
    for c in $(git rev-list "$range" 2>/dev/null); do
        m="$(git log -1 --format='%B' "$c" | grep -iwE "$PATTERN" | grep -viE "$ALLOW" || true)"
        [ -n "$m" ] && hits="${hits}${hits:+$'\n'}$(git log -1 --format='%h' "$c"): $(printf '%s' "$m" | head -1)"
    done
    [ -z "$hits" ] || { report "commit messages being pushed" "$hits"; exit 1; }
    echo "private-refs: clean — no sibling-project names in the commit messages being pushed."
    exit 0
    ;;
esac

# This file is excluded from its own scan: PATTERN necessarily spells out every name,
# so including it would be a guaranteed self-hit. (Standard for a linter's own rule
# definitions. It does mean this one file is a blind spot — keep it to the pattern.)
hits="$(git grep -nIwiE "$PATTERN" -- . ':!scripts/check_no_private_refs.sh' 2>/dev/null | grep -viE "$ALLOW" || true)"
[ -z "$hits" ] || { report "the public tree" "$hits"; exit 1; }
echo "private-refs: clean — no sibling-project names in the public tree."
