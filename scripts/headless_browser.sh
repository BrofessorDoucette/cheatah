#!/usr/bin/env bash
# headless_browser.sh — run a browser that CANNOT touch the developer's session.
#
# Source this and call `hb_run <url-or-args...>`, or run it directly:
#     scripts/headless_browser.sh --dump-dom "file:///tmp/x.html"
#     scripts/headless_browser.sh --screenshot=/tmp/shot.png --window-size=1280,900 "file://…"
#
# WHY THIS EXISTS. A one-off screenshot command during the WCAG work opened a real window and
# a file:// tab in the developer's OWN browser, and sat on a dialog until its timeout killed
# it. The cause is Firefox's remote protocol: `firefox <url>` hands the URL to an
# ALREADY-RUNNING instance and can ignore --headless entirely. Nothing about that is exotic —
# it is the documented default — so any script that shells out to a browser will eventually
# do it to somebody.
#
# The containment is therefore structural, not a convention to remember:
#
#   Firefox   --no-remote + MOZ_NO_REMOTE=1   refuses to talk to a running instance and
#                                             starts its own, so no tab can ever appear in a
#                                             session we do not own. THIS IS THE ONE THAT
#                                             MATTERS — do not delete it as redundant.
#             --profile <temp>                never reads or writes a real profile
#             --headless                      no window
#
#   Chrome    --user-data-dir <temp>          never reads or writes a real profile
#             --headless=new                  no window
#             --no-first-run                  cannot show the first-run/welcome prompt
#             --no-default-browser-check      cannot show the "make default?" prompt
#             --disable-extensions            no extension can observe or alter the page
#
# Both get a temp profile that is deleted on exit, so a run leaves nothing behind.
#
# $BROWSER picks the engine explicitly; otherwise Chrome is preferred (its headless mode is
# the better-behaved of the two) and Firefox is the fallback.
set -uo pipefail

# The temp profile lives for one call and is removed even on failure.
hb_run() {
    local bin="" kind=""
    if [ -n "${BROWSER:-}" ]; then
        bin="$BROWSER"
        case "$bin" in *firefox*) kind=firefox ;; *) kind=chrome ;; esac
    else
        local c
        for c in google-chrome google-chrome-stable chromium chromium-browser; do
            command -v "$c" >/dev/null 2>&1 && { bin="$c"; kind=chrome; break; }
        done
        if [ -z "$bin" ] && command -v firefox >/dev/null 2>&1; then
            bin=firefox; kind=firefox
        fi
    fi
    [ -n "$bin" ] || { echo "headless_browser: no Chrome/Chromium or Firefox found (set \$BROWSER)" >&2; return 127; }

    local prof rc
    prof="$(mktemp -d)" || return 1

    if [ "$kind" = firefox ]; then
        # MOZ_NO_REMOTE belts what --no-remote braces: older builds honour only the env var.
        MOZ_NO_REMOTE=1 timeout "${HB_TIMEOUT:-120}" "$bin" \
            --no-remote --new-instance --profile "$prof" --headless "$@"
        rc=$?
    else
        timeout "${HB_TIMEOUT:-120}" "$bin" \
            --headless=new --disable-gpu --no-sandbox \
            --user-data-dir="$prof" --no-first-run --no-default-browser-check \
            --disable-extensions "$@"
        rc=$?
    fi

    rm -rf "$prof"
    return $rc
}

# Report which engine would be used, without launching anything.
hb_engine() {
    if [ -n "${BROWSER:-}" ]; then echo "$BROWSER"; return 0; fi
    local c
    for c in google-chrome google-chrome-stable chromium chromium-browser firefox; do
        command -v "$c" >/dev/null 2>&1 && { echo "$c"; return 0; }
    done
    return 1
}

# Executed rather than sourced: treat the arguments as one contained run.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    hb_run "$@"
fi
