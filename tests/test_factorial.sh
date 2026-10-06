#!/usr/bin/env bash
set -euo pipefail
for pair in '0 1' '1 1' '5 120' '10 3628800' '12 479001600'; do
  read -r input expected <<< "$pair"
  output="$(./user/edu_cli factorial "$input")"
  grep -q "factorial($input) = $expected" <<< "$output"
  echo "$output"
done
if ./user/edu_cli factorial 13 >/dev/null 2>&1; then
  echo 'factorial(13) unexpectedly accepted' >&2
  exit 1
fi
echo 'Factorial values and range rejection: PASS'

