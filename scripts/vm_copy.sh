#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
[[ $# -eq 2 ]] || { echo "Usage: $0 LOCAL_PATH GUEST_PATH" >&2; exit 2; }
scp -i "$KEY_FILE" -P "$(vm_port)" -o BatchMode=yes \
  -o StrictHostKeyChecking=accept-new -o "UserKnownHostsFile=$KNOWN_HOSTS" \
  "$1" "$VM_USER@$VM_HOST:$2"

