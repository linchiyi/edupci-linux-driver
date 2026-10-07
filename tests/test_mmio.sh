#!/usr/bin/env bash
set -euo pipefail
for value in 0x00000000 0xffffffff 0x12345678 0xa5a5a5a5; do
  ./user/edu_cli invert "$value"
done
for value in -0 -1; do
  if ./user/edu_cli invert "$value" >/dev/null 2>&1; then
    echo "negative integer $value unexpectedly accepted" >&2
    exit 1
  fi
done
echo 'MMIO patterns: PASS'
