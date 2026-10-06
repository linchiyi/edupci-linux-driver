#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
vm_ssh 'set -euo pipefail; if grep -q "^edupci " /proc/modules; then sudo rmmod edupci; fi; if grep -q "^edupci " /proc/modules; then exit 1; fi; sudo dmesg | tail -n 20' \
  2>&1 | tee "$ROOT_DIR/evidence/module_unload.txt"
