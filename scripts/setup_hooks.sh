#!/usr/bin/env bash
# One-time setup: point git at the tracked .githooks/ directory so the pre-push
# QA gate runs for everyone who clones this repo. Re-run safely any time.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

git config core.hooksPath .githooks
chmod +x .githooks/* scripts/*.sh 2>/dev/null || true

echo "core.hooksPath = $(git config --get core.hooksPath)"
echo "Pre-push QA gate is active. A 'git push' now runs scripts/qa_gate.sh first."
echo "Run the gate manually any time:  bash scripts/qa_gate.sh"
