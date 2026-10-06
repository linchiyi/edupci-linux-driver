#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
"$ROOT_DIR/scripts/vm_stop.sh"
rm -f "$VM_DIR/images/edupci-guest.qcow2"
"$ROOT_DIR/scripts/vm_setup.sh"
"$ROOT_DIR/scripts/vm_start.sh"

