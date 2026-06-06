#!/usr/bin/env bash
# Convenience wrapper: push through the QA gate.
#
# This just runs `git push` with your args — the git pre-push hook
# (.githooks/pre-push -> scripts/qa_gate.sh) does the actual gating, so a plain
# `git push` is gated too. Using this wrapper also verifies the hook is enabled.
#
#   scripts/qa_push.sh                  # like: git push
#   scripts/qa_push.sh origin main      # explicit remote/branch
#   scripts/qa_push.sh -u origin feature-x
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

hooks_path="$(git config --get core.hooksPath || true)"
if [ "$hooks_path" != ".githooks" ]; then
    echo "WARNING: core.hooksPath is not .githooks — the QA gate may not run."
    echo "         Enable it once with: scripts/setup_hooks.sh"
fi

exec git push "$@"
