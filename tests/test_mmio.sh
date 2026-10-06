#!/usr/bin/env bash
set -euo pipefail
for value in 0x00000000 0xffffffff 0x12345678 0xa5a5a5a5; do
  ./user/edu_cli invert "$value"
done
echo 'MMIO patterns: PASS'

