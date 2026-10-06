#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../scripts/lib.sh"
mkdir -p "$ROOT_DIR/evidence"
cleanup() {
  vm_ssh 'sudo rmmod edupci 2>/dev/null || true' >/dev/null 2>&1 || true
}
trap cleanup EXIT
"$ROOT_DIR/scripts/vm_start.sh"
"$ROOT_DIR/scripts/sync_to_guest.sh"
vm_ssh 'sudo rmmod edupci 2>/dev/null || true'
"$ROOT_DIR/scripts/build_guest.sh"
"$ROOT_DIR/scripts/load_driver.sh"
: > "$ROOT_DIR/evidence/full_test.txt"
run_test() {
  local name="$1" script="$2"
  {
    printf '\n=== %s ===\n' "$script"
    vm_ssh "cd ~/edupci && sudo bash tests/$script"
  } 2>&1 | tee "$ROOT_DIR/evidence/$name" | tee -a "$ROOT_DIR/evidence/full_test.txt"
}
run_test device_test.txt test_device.sh
run_test mmio_test.txt test_mmio.sh
run_test factorial_irq_test.txt test_factorial.sh
run_test ring_test.txt test_events.sh
run_test dma_test.txt test_dma.sh
run_test reload_test.txt test_reload.sh
echo 'EduPCI integration suite: PASS' | tee -a "$ROOT_DIR/evidence/full_test.txt"
"$ROOT_DIR/scripts/collect_evidence.sh"
echo 'All EduPCI integration tests passed.'
