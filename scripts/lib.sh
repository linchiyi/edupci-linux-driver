#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VM_DIR="$ROOT_DIR/.vm"
KEY_FILE="$VM_DIR/keys/id_edupci"
KNOWN_HOSTS="$VM_DIR/keys/known_hosts"
VM_USER="edupci"
VM_HOST="127.0.0.1"
SSH_PORT_FILE="$VM_DIR/run/ssh_port"

vm_port() {
  if [[ -f "$SSH_PORT_FILE" ]]; then cat "$SSH_PORT_FILE"; else printf '2222\n'; fi
}

vm_ssh() {
  ssh -i "$KEY_FILE" -p "$(vm_port)" -o BatchMode=yes -o ConnectTimeout=8 \
    -o StrictHostKeyChecking=accept-new -o "UserKnownHostsFile=$KNOWN_HOSTS" \
    "$VM_USER@$VM_HOST" "$@"
}

wait_for_ssh() {
  local tries=${1:-60} n
  for ((n = 1; n <= tries; n++)); do
    if vm_ssh true >/dev/null 2>&1; then return 0; fi
    sleep 3
  done
  printf 'Guest SSH did not become ready after %s attempts. See %s/logs/serial.log.\n' \
    "$tries" "$VM_DIR" >&2
  return 1
}

