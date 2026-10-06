#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
PID_FILE="$VM_DIR/run/qemu.pid"
if [[ ! -f "$PID_FILE" ]] || ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
  rm -f "$PID_FILE"
  echo 'QEMU is already stopped.'
  exit 0
fi
pid="$(cat "$PID_FILE")"
vm_ssh 'sudo systemctl poweroff' >/dev/null 2>&1 || true
for _ in $(seq 1 20); do
  if ! kill -0 "$pid" 2>/dev/null; then break; fi
  sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
  echo 'Guest did not power off cleanly; sending QEMU SIGTERM.'
  kill -TERM "$pid"
  for _ in $(seq 1 10); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
fi
rm -f "$PID_FILE"
echo 'QEMU guest stopped.'

