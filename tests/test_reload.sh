#!/usr/bin/env bash
set -euo pipefail
for cycle in 1 2 3; do
  rmmod edupci
  insmod ./driver/edupci.ko
  test -c /dev/edupci
  ./user/edu_cli factorial 5 | grep -q 'factorial(5) = 120'
  echo "reload cycle $cycle: PASS"
done
rmmod edupci

