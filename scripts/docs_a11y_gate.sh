#!/usr/bin/env bash
# Docs accessibility gate — WCAG 2.1 AA over the generated site (docs/html).
#
# WHY THIS EXISTS: the header grid hard-coded a 248px sidebar column and a 224px TOC column
# at EVERY breakpoint. Below 1000px their content was hidden but the tracks stayed, so the
# header kept a ~526px floor and the whole document scrolled sideways on every phone. It
# shipped for months because nothing ever measured the site at a phone width.
#
# Two halves:
#   reflow   headless Chrome, WCAG 2.1 SC 1.4.10: no horizontal scrolling down to 320px
#   static   scripts/a11y_check.purr — contrast maths + per-page structure, in cheatah
#
# THE 500px TRAP: `chrome --window-size=320,800` does NOT give a 320px viewport. Chrome
# clamps its window to a 500px minimum, so a naive gate silently measures 500px and passes
# while a phone is still broken. We therefore load each page in an IFRAME of an exact width
# and measure the inner document — that honours media queries at any width. Verified: with
# the old stylesheet this reports 550px at a 320px viewport (FAIL); with the fix, 310px.
#
#   scripts/docs_a11y_gate.sh            # gate docs/html
#   DOCS_HTML=... scripts/docs_a11y_gate.sh
#
# Sibling repos reuse this gate rather than forking it (see deploy/scripts/a11y_gate.sh):
# every path and threshold is an env knob, so paths here are resolved from THIS script's
# location, never from the caller's working directory.
#   DOCS_HTML   tree of pages to check      A11Y_CSS     stylesheet to read tokens from
#   A11Y_PAIRS  contrast obligations        A11Y_LIGHT=0 no light palette expected
#   A11Y_PAGES  representative page list    A11Y_WIDTHS  viewport widths
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHEATAH_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DOCS_HTML="${DOCS_HTML:-$CHEATAH_ROOT/docs/html}"
A11Y_CSS="${A11Y_CSS:-$CHEATAH_ROOT/docs/gen/assets/cheatah-docs.css}"
WIDTHS="${A11Y_WIDTHS:-320 390 768 1280}"
bold() { printf '\033[1m[a11y-gate] %s\033[0m\n' "$*"; }
fail() { printf '\033[31m[a11y-gate] FAILED: %s\033[0m\n' "$*"; exit 1; }

# A11Y_REQUIRE=1 turns every "skipped because the tooling is missing" into a failure.
# The dev path may skip loudly — a contributor without Chrome still gets the static half —
# but a DEPLOY must not: a skipped check that reads as a pass is how the broken site
# reached production in the first place. deploy/ sets this.
REQUIRE="${A11Y_REQUIRE:-0}"
skip_or_fail() {
    [ "$REQUIRE" = "1" ] && fail "$1 (A11Y_REQUIRE=1)"
    printf '\033[33m[a11y-gate] SKIPPED %s\033[0m\n' "$1"
}

[ -d "$DOCS_HTML" ] || fail "$DOCS_HTML does not exist (run docs/build-docs.sh first)"

# ---- static half: contrast + page structure, in cheatah ---------------------------------
PURRC="${PURRC:-$CHEATAH_ROOT/build/debug/bin/purrc}"
RUNTIME="${RUNTIME:-$CHEATAH_ROOT/build/debug/bin/cheatah}"
if [ -x "$PURRC" ] && [ -x "$RUNTIME" ]; then
    SO="$(mktemp -d)/a11y_check.so"
    bold "static checks (contrast + page structure)…"
    "$PURRC" "$SCRIPT_DIR/a11y_check.purr" -o "$SO" >/dev/null || fail "purrc a11y_check.purr"
    DOCS_HTML="$DOCS_HTML" A11Y_CSS="$A11Y_CSS" "$RUNTIME" "$SO" || fail "static accessibility checks"
else
    # Loud, not silent: a skipped check must never read as a passing one.
    skip_or_fail "static half: $PURRC / $RUNTIME not built"
fi

# ---- reflow half: real viewport widths in a real engine ---------------------------------
# The browser runs through scripts/headless_browser.sh, which confines it to a throwaway
# profile and — for Firefox — disables the remote protocol that hands URLs to an already
# running instance. Read that file before changing anything here: a plain `firefox <url>`
# once opened a real tab in a developer's own browser.
# shellcheck source=/dev/null
. "$SCRIPT_DIR/headless_browser.sh"
if ! ENGINE="$(hb_engine)"; then
    skip_or_fail "reflow half: no Chrome/Chromium or Firefox found (set \$BROWSER)"
    bold "static half passed; reflow UNVERIFIED on this machine."
    exit 0
fi

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
cat > "$WORK/probe.html" <<'HTML'
<!DOCTYPE html><html><body><iframe id="f" style="border:0;height:700px"></iframe><script>
// Measure each page inside a fixed-width iframe: --window-size clamps at 500px, an iframe
// does not, and media queries resolve against the iframe's own viewport.
//
// The measurement is POSTED OUT by the page rather than read in from here. Reaching into
// f.contentDocument across file:// origins needs --allow-file-access-from-files, which
// relaxes the same-origin policy for the whole browser instance; postMessage is
// cross-origin by design and needs no such flag. The measured copy carries the tiny
// reporter that the gate injects.
const q = new URLSearchParams(location.search);
const page = q.get('p'), widths = q.get('w').split(',').map(Number);
const f = document.getElementById('f'), out = [];
function at(w) {
  return new Promise(res => {
    let done = false;
    const finish = v => { if (!done) { done = true; out.push(w + ':' + v); cleanup(); res(); } };
    const onMsg = e => {
      if (e.data && e.data.a11y === 'reflow') finish(e.data.scrollWidth);
    };
    const cleanup = () => { window.removeEventListener('message', onMsg); clearTimeout(t); };
    window.addEventListener('message', onMsg);
    // A page that never reports is a failure, not a hang: -1 can never be <= the width.
    const t = setTimeout(() => finish(-1), 8000);
    f.style.width = w + 'px';
    f.src = page + '?w=' + w;      // vary the URL so each width really reloads
  });
}
(async () => { for (const w of widths) await at(w); document.title = 'R ' + out.join(' '); })();
</script></body></html>
HTML

# The reporter injected into each measured copy. It runs after layout settles and posts the
# document's own scrollWidth to the harness.
REPORTER='<script>(function(){function s(){parent.postMessage({a11y:"reflow",scrollWidth:document.documentElement.scrollWidth},"*")}addEventListener("load",function(){setTimeout(s,60)});if(document.readyState==="complete")setTimeout(s,60)})();</script>'

# A representative page of each KIND the generator emits — a landing (no TOC), a module
# reference, a class page, a long prose guide, a source listing, and an extension subsite.
# A caller with a different site shape passes its own list in A11Y_PAGES; a tree that
# matches neither gets every page, so a new site is never silently unchecked.
PAGES="${A11Y_PAGES:-}"
if [ -z "$PAGES" ]; then
    for cand in index.html namespacecheatah_1_1math.html classcheatah_1_1memory_1_1Owner.html \
                performance.html biome.html; do
        [ -f "$DOCS_HTML/$cand" ] && PAGES="$PAGES $cand"
    done
    for extra in "$DOCS_HTML"/src_stdlib_*.html; do
        [ -e "$extra" ] && { PAGES="$PAGES $(basename "$extra")"; break; }
    done
    for sub in "$DOCS_HTML"/cheatah-*/index.html; do
        [ -e "$sub" ] && { PAGES="$PAGES $(basename "$(dirname "$sub")")/index.html"; break; }
    done
    if [ -z "${PAGES// /}" ]; then
        PAGES="$(cd "$DOCS_HTML" && find . -name '*.html' | sed 's|^\./||' | sort)"
    fi
fi

ABS="$(cd "$DOCS_HTML" && pwd)"
CSV="$(echo "$WIDTHS" | tr ' ' ',')"
mkdir -p "$WORK/pages"
bad=0; checked=0
bold "reflow (WCAG 1.4.10) at ${WIDTHS// /, }px…"
for p in $PAGES; do
    [ -f "$ABS/$p" ] || continue
    checked=$((checked + 1))
    # Measure a COPY carrying the reporter. The copy needs a <base> pointing back at the
    # page's real directory: without it the relative stylesheet does not resolve, the copy
    # renders unstyled, and an unstyled page reflows fine — the gate would pass everything.
    src_dir="$(cd "$(dirname "$ABS/$p")" && pwd)"
    copy="$WORK/pages/$(echo "$p" | tr '/' '_')"
    awk -v base="<base href=\"file://$src_dir/\">" -v rep="$REPORTER" '
        !done_base && /<head[^>]*>/ { sub(/<head[^>]*>/, "&" base); done_base = 1 }
        /<\/body>/ && !done_rep    { sub(/<\/body>/, rep "&"); done_rep = 1 }
        { print }
    ' "$ABS/$p" > "$copy"
    title="$(hb_run --virtual-time-budget=20000 --dump-dom --window-size=1400,900 \
        "file://$WORK/probe.html?w=$CSV&p=file://$copy" 2>/dev/null \
        | grep -o '<title>R [^<]*</title>' | sed 's/<[^>]*>//g; s/^R //')"
    [ -n "$title" ] || { echo "  a11y: FAIL $p - probe produced no measurement"; bad=$((bad + 1)); continue; }
    for m in $title; do
        w="${m%%:*}"; got="${m##*:}"
        if [ "$got" -gt "$w" ]; then
            echo "  a11y: FAIL $p - ${got}px wide in a ${w}px viewport ($((got - w))px of horizontal scroll)"
            bad=$((bad + 1))
        fi
    done
done

[ "$checked" -gt 0 ] || fail "no representative pages found under $DOCS_HTML"
[ "$bad" -eq 0 ] || fail "$bad reflow violation(s) — a fixed grid track is outliving the breakpoint that hides it"
bold "PASSED — $checked pages reflow cleanly down to ${WIDTHS%% *}px."
