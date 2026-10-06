#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
vm_ssh 'set -euo pipefail; if ! grep -q "^edupci " /proc/modules; then sudo insmod ~/edupci/driver/edupci.ko; fi; test -c /dev/edupci; lsmod | grep "^edupci "; sudo dmesg | tail -n 20' \
  2>&1 | tee "$ROOT_DIR/evidence/module_load.txt"
