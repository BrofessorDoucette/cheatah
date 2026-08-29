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
# "BigBrain" — the company — is LEGITIMATE anywhere: copyright headers, the docs footer,
# the topbar backlink. So is the storefront URL https://bigbrain-technology.com: it is the
# company's PUBLIC sales page, not a private project, and the docs site links to it on
# every page. What this check exists to stop is cheatah describing how the products WORK;
# naming the shop that funds the language is the opposite of a leak.
#
# Allowlisting SCRUBS the allowed phrase and re-tests the line — it does not drop the
# line. That distinction matters: "see bigbrain-technology.com for how godspeed does X"
# must still FAIL on "godspeed", which a whole-line drop would have let through.
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
#   check_no_private_refs.sh --release [TAG]  scan a PUBLISHED GitHub release's title and body
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

PATTERN='bigbrain|atomizer|godspeed|sherlock|conjure|looking-glass|lookingglass|scribe|zebra|glgan|alice'

# Private project NAMES are not the only way to leak. A release note once described "exactly two
# compute layers across the ecosystem" and attributed the second to a product this repo does not
# name — no private name anywhere in it, so the scan above passed and it published. Architecture
# INTENT is the second leak class, and it needs its own phrases.
#
# Kept deliberately narrow, and the narrowness is the point. "game engine" is NOT here: README
# links the product page in its ecosystem list on purpose. Neither is the SINGULAR "compute layer" —
# cheatah-plot's own docs say "the compute layer the renderer dispatches on", which names the one it
# uses and discloses nothing. What leaks is a claim about how MANY exist, so the plural is the
# signal, plus the ordinal form that implies a second without pluralising.
PATTERN="$PATTERN|compute layers|second compute layer"
ALLOW='BigBrain|(https://)?bigbrain-technology\.com[^ "<)]*'

# Keep only the lines that still match PATTERN once the allowlisted phrases are removed.
# Scrubbing (rather than dropping the whole line) is what stops an allowlisted phrase from
# smuggling a real leak past the check on the same line. The ORIGINAL line is what gets
# reported, so the reader sees the hit in its true context.
#
# ALLOW is matched CASE-SENSITIVELY (no sed /I) while PATTERN stays case-insensitive: only
# the brand in its real casing — "BigBrain", "https://bigbrain-technology.com" — is waived.
# A stray lower-case "bigbrain" somewhere else still fails, as it should.
filter_allowed() {
    while IFS= read -r line; do
        if printf '%s\n' "$line" | sed -E "s@$ALLOW@@g" | grep -qIwiE "$PATTERN"; then
            printf '%s\n' "$line"
        fi
    done
    return 0
}

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
    hits="$(grep -nIwiE "$PATTERN" "$file" 2>/dev/null | grep -v '^[0-9]*:#' | filter_allowed || true)"
    [ -z "$hits" ] || { report "this commit message" "$hits"; exit 1; }
    exit 0
    ;;
--release)
    # A release's notes are generated from CHANGELOG.md at release time and then live on GitHub as
    # their OWN artifact. Fixing the changelog afterwards does not touch them — which is exactly how
    # a published release came to describe private architecture long after the tree was clean. The
    # tree scan cannot see them, so this mode reads them back from the API and applies the same
    # pattern. Skips (rather than fails) when gh is absent or unauthenticated, so an offline gate
    # run does not block on it; the release procedure is where it must actually pass.
    command -v gh >/dev/null 2>&1 || { echo "private-refs: gh not installed — release scan SKIPPED"; exit 0; }
    tag="${2:-}"
    if [ -z "$tag" ]; then
        tag="$(gh release view --json tagName -q .tagName 2>/dev/null || true)"
    fi
    [ -n "$tag" ] || { echo "private-refs: no release to scan — SKIPPED"; exit 0; }
    notes="$(gh release view "$tag" --json name,body -q '.name + "\n" + .body' 2>/dev/null || true)"
    [ -n "$notes" ] || { echo "private-refs: could not read release $tag — SKIPPED"; exit 0; }
    hits="$(printf '%s\n' "$notes" | grep -nIwiE "$PATTERN" | filter_allowed || true)"
    [ -z "$hits" ] || { report "the published release $tag" "$hits"; exit 1; }
    echo "private-refs: clean — no sibling-project names in the published release $tag."
    exit 0
    ;;
--range)
    # A git revision RANGE, unquoted on purpose: it may be "A..B" or a multi-token selector such as
    # "<sha> --not --remotes=origin", which is what a new ref (a tag) needs — see .githooks/pre-push.
    range="${2:?--range needs a git revision range}"
    hits=""
    # shellcheck disable=SC2086  # word splitting is the point: $range may carry several tokens.
    for c in $(git rev-list $range 2>/dev/null); do
        m="$(git log -1 --format='%B' "$c" | grep -iwE "$PATTERN" | filter_allowed || true)"
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
hits="$(git grep -nIwiE "$PATTERN" -- . ':!scripts/check_no_private_refs.sh' 2>/dev/null | filter_allowed || true)"
[ -z "$hits" ] || { report "the public tree" "$hits"; exit 1; }
echo "private-refs: clean — no sibling-project names in the public tree."
