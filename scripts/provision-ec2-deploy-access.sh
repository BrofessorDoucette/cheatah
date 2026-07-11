#!/usr/bin/env bash
# provision-ec2-deploy-access.sh — create a dedicated, minimal-power deploy identity
# for the cheatah docs EC2 box, safely.
#
# What "safely" means here:
#   - A FRESH ed25519 keypair used for nothing else; the private key never leaves
#     ~/.ssh on this machine and is never printed.
#   - A dedicated non-sudo `deploy` user on the instance — the key can rsync the
#     docs and (re)start the docs server, and that is all. Your admin key stays
#     out of automated hands entirely.
#   - The deploy key is passphrase-LESS on purpose: headless agent sessions cannot
#     answer a keyring prompt (that is what broke pushes on release night). Its
#     safety comes from its scope (non-sudo user, one box), not a passphrase.
#     Your personal admin key should keep its passphrase.
#
# Usage:
#   scripts/provision-ec2-deploy-access.sh <ec2-host-or-ip> [admin-user]
#
#   <ec2-host-or-ip>  the instance's public DNS or IP
#   [admin-user]      an existing user you can already SSH into (default: ubuntu)
#
# Requires: your normal admin SSH access to the instance (used once, to plant the
# public key). Nothing here touches AWS credentials or the instance's IAM role.
set -euo pipefail

HOST="${1:?usage: $0 <ec2-host-or-ip> [admin-user]}"
ADMIN="${2:-ubuntu}"
KEY="$HOME/.ssh/cheatah_docs_deploy_ed25519"
ALIAS="cheatah-docs"

# 1. Fresh, single-purpose keypair (skip if it already exists).
if [ ! -f "$KEY" ]; then
    ssh-keygen -t ed25519 -f "$KEY" -N "" -C "cheatah-docs-deploy"
    echo "[provision] generated $KEY (private key stays here, 600)"
else
    echo "[provision] $KEY already exists — reusing it"
fi
chmod 600 "$KEY"
PUB="$(cat "${KEY}.pub")"

# 2. Dedicated non-sudo deploy user on the instance, owning /srv/cheatah-docs.
#    Uses your admin access ONCE; idempotent on re-runs.
ssh "${ADMIN}@${HOST}" "sudo bash -s" <<EOF
set -euo pipefail
id -u deploy >/dev/null 2>&1 || useradd -m -s /bin/bash deploy
install -d -m 700 -o deploy -g deploy /home/deploy/.ssh
grep -qF "${PUB}" /home/deploy/.ssh/authorized_keys 2>/dev/null || \
    echo "${PUB}" >> /home/deploy/.ssh/authorized_keys
chown deploy:deploy /home/deploy/.ssh/authorized_keys
chmod 600 /home/deploy/.ssh/authorized_keys
install -d -o deploy -g deploy /srv/cheatah-docs
echo "[instance] deploy user ready; /srv/cheatah-docs owned by deploy"
EOF

# 3. SSH config alias so every later command is just: ssh cheatah-docs …
if ! grep -q "^Host ${ALIAS}\$" "$HOME/.ssh/config" 2>/dev/null; then
    {
        echo ""
        echo "Host ${ALIAS}"
        echo "    HostName ${HOST}"
        echo "    User deploy"
        echo "    IdentityFile ${KEY}"
        echo "    IdentitiesOnly yes"
    } >> "$HOME/.ssh/config"
    chmod 600 "$HOME/.ssh/config"
    echo "[provision] added 'Host ${ALIAS}' to ~/.ssh/config"
else
    echo "[provision] ~/.ssh/config already has 'Host ${ALIAS}' — left as is"
fi

# 4. Prove it works, non-interactively (exactly how the agent will use it).
ssh -o BatchMode=yes -o ConnectTimeout=10 "${ALIAS}" \
    'echo "[instance] deploy login OK: $(whoami)@$(hostname)"'

echo ""
echo "[provision] done. The agent can now use:  ssh ${ALIAS} / rsync … ${ALIAS}:/srv/cheatah-docs"
echo "[provision] security-group reminder: 22 open to YOUR IP only; 80 (and later 443) public."
echo "[provision] nothing secret was printed; the private key never leaves this machine."
