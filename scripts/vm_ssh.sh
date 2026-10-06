#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
if [[ $# -eq 0 ]]; then
  exec ssh -i "$KEY_FILE" -p "$(vm_port)" -o ConnectTimeout=8 \
    -o StrictHostKeyChecking=accept-new -o "UserKnownHostsFile=$KNOWN_HOSTS" \
    -t "$VM_USER@$VM_HOST"
fi
vm_ssh "$@"
