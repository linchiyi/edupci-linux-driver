#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
wait_for_ssh
vm_ssh 'set -eu; cd ~/edupci; make -C driver clean; make -C user clean all; make -C driver' \
  2>&1 | tee "$ROOT_DIR/evidence/build.log"
echo 'Guest kernel module and CLI build completed.'

