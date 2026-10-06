#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
PID_FILE="$VM_DIR/run/qemu.pid"
if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
  printf 'QEMU: running (PID %s)\n' "$(cat "$PID_FILE")"
  if vm_ssh true >/dev/null 2>&1; then echo 'Guest SSH: ready'; else echo 'Guest SSH: not ready'; fi
else
  echo 'QEMU: stopped'
fi

