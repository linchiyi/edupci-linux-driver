#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
"$ROOT_DIR/scripts/load_driver.sh" >/dev/null
{
  echo '[1/6] Guest / PCI device'
  vm_ssh 'lspci -nn -d 1234:11e8'
  echo
  echo '[2/6] Driver probe'
  vm_ssh 'lsmod | grep "^edupci "; test -c /dev/edupci; echo "device node: /dev/edupci"'
  echo 'PASS'
  echo
  echo '[3/6] MMIO inversion'
  for value in 0x00000000 0xffffffff 0x12345678 0xa5a5a5a5; do
    vm_ssh "sudo ~/edupci/user/edu_cli invert $value"
  done
  echo 'PASS'
  echo
  echo '[4/6] Interrupt factorial'
  vm_ssh 'sudo ~/edupci/user/edu_cli factorial 5'
  echo 'IRQ completion: PASS'
  echo
  echo '[5/6] Ring/event path'
  vm_ssh 'sudo ~/edupci/user/edu_cli ring-test'
  echo
  echo '[6/6] DMA'
  vm_ssh 'sudo ~/edupci/user/edu_cli dma-test'
  echo
  echo 'EduPCI demo: PASS'
} 2>&1 | tee "$ROOT_DIR/evidence/demo_output.txt"

