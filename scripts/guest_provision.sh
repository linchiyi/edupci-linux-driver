#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"
wait_for_ssh
echo 'Installing build and inspection tools in the guest.'
vm_ssh 'sudo apt-get update && sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential "linux-headers-$(uname -r)" pciutils kmod git make gcc g++ strace python3'
vm_ssh 'bash -s' <<'REMOTE' | tee "$ROOT_DIR/evidence/guest_environment.txt"
set -eu
echo '=== uname -a ==='
uname -a
echo '=== uname -m ==='
uname -m
echo '=== /etc/os-release ==='
cat /etc/os-release
echo '=== lspci -nn ==='
lspci -nn
echo '=== EDU device ==='
lspci -nn -d 1234:11e8 | tee /tmp/edupci-lspci.txt
grep -q '1234:11e8' /tmp/edupci-lspci.txt
echo '=== kernel build directory ==='
test -e "/lib/modules/$(uname -r)/build"
ls -ld "/lib/modules/$(uname -r)/build"
echo '=== gcc ==='
gcc --version | head -1
echo '=== make ==='
make --version | head -1
REMOTE
vm_ssh 'lspci -nn -d 1234:11e8' | tee "$ROOT_DIR/evidence/lspci_before_driver.txt"
echo 'Guest provisioning and EDU PCI discovery verified.'

