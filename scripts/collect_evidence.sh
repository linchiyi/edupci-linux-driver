#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
mkdir -p "$ROOT_DIR/evidence"
vm_ssh 'lspci -nn; echo; ls -l /dev/edupci 2>/dev/null || true; echo; lsmod | grep edupci || true' \
  > "$ROOT_DIR/evidence/device_state.txt"
vm_ssh 'sudo dmesg | grep -Ei "edupci|BUG:|WARNING:|Oops:|panic|general protection|use-after-free|KASAN|hung task|soft lockup|hard LOCKUP" || true' \
  > "$ROOT_DIR/evidence/dmesg_driver.txt"
vm_ssh 'sudo dmesg | grep -Ei "BUG:|WARNING:|Oops:|kernel panic|general protection fault|use-after-free|KASAN|hung task|soft lockup|hard LOCKUP" || true' \
  > "$ROOT_DIR/evidence/kernel_health_check.txt"
echo "Captured guest state and kernel logs under $ROOT_DIR/evidence/"
