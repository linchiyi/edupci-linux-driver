#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
mkdir -p "$ROOT_DIR/evidence"
vm_ssh 'lspci -nn; echo; ls -l /dev/edupci 2>/dev/null || true; echo; lsmod | grep edupci || true' \
  > "$ROOT_DIR/evidence/device_state.txt"
vm_ssh 'sudo dmesg' > "$ROOT_DIR/evidence/dmesg_snapshot.txt"
grep -Ei 'edupci|BUG:|WARNING:|Oops:|panic|general protection|use-after-free|KASAN|hung task|soft lockup|hard LOCKUP' \
  "$ROOT_DIR/evidence/dmesg_snapshot.txt" \
  > "$ROOT_DIR/evidence/dmesg_driver.txt" || true
if grep -Eiq 'BUG:|WARNING:|Oops:|kernel panic|general protection fault|use-after-free|KASAN|hung task|soft lockup|hard LOCKUP' \
  "$ROOT_DIR/evidence/dmesg_snapshot.txt"; then
  grep -Ei 'BUG:|WARNING:|Oops:|kernel panic|general protection fault|use-after-free|KASAN|hung task|soft lockup|hard LOCKUP' \
    "$ROOT_DIR/evidence/dmesg_snapshot.txt" \
    > "$ROOT_DIR/evidence/kernel_health_check.txt"
  echo 'FAIL: matching kernel health messages were found.' \
    >> "$ROOT_DIR/evidence/kernel_health_check.txt"
else
  echo 'PASS: no BUG, WARNING, Oops, panic, protection fault, use-after-free, KASAN, hung-task, or lockup messages.' \
    > "$ROOT_DIR/evidence/kernel_health_check.txt"
fi
echo "Captured guest state and kernel logs under $ROOT_DIR/evidence/"
