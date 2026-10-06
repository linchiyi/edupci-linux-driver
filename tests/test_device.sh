#!/usr/bin/env bash
set -euo pipefail
lspci -nn -d 1234:11e8 | grep -q '1234:11e8'
test -c /dev/edupci
bound=0
for path in /sys/bus/pci/devices/*; do
  [[ -r "$path/vendor" && -r "$path/device" ]] || continue
  if [[ "$(cat "$path/vendor")" == 0x1234 && "$(cat "$path/device")" == 0x11e8 && -L "$path/driver" ]]; then
    bound=1
  fi
done
test "$bound" -eq 1
echo 'PCI discovery, driver binding, and character node: PASS'

