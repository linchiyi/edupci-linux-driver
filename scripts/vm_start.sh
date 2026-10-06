#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
PID_FILE="$VM_DIR/run/qemu.pid"
if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
  echo "QEMU is already running (PID $(cat "$PID_FILE"))."
else
  "$ROOT_DIR/scripts/vm_setup.sh"
  rm -f "$PID_FILE"
  port="$(vm_port)"
  if nc -z -w 1 127.0.0.1 "$port" >/dev/null 2>&1; then
    echo "SSH forwarding port $port is already occupied." >&2
    exit 1
  fi
  qemu-system-x86_64 \
    -name edupci-guest -machine q35,accel=kvm -cpu host -smp 2 -m 2G \
    -nodefaults -no-user-config \
    -drive "file=$VM_DIR/images/edupci-guest.qcow2,if=virtio,format=qcow2" \
    -drive "file=$VM_DIR/cloud-init/seed.iso,if=virtio,format=raw,readonly=on" \
    -device edu \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$port-:22" \
    -device virtio-net-pci,netdev=net0 \
    -display none -serial "file:$VM_DIR/logs/serial.log" -monitor none \
    -pidfile "$PID_FILE" -daemonize -D "$VM_DIR/logs/qemu.log"
  printf 'Started QEMU guest PID %s; SSH port %s.\n' "$(cat "$PID_FILE")" "$port"
fi
echo 'Waiting for cloud-init and SSH...'
wait_for_ssh 60
echo 'Guest SSH is ready.'
