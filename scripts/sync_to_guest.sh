#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
wait_for_ssh
tar -C "$ROOT_DIR" --exclude='./.git' --exclude='./.vm' --exclude='./evidence' \
  -czf - . | vm_ssh 'mkdir -p ~/edupci && tar -xzf - -C ~/edupci'
echo 'Synced project sources to the guest.'

