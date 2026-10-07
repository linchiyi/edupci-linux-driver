# Verification report

Final verification was refreshed on 2026-10-07 in the existing QEMU/KVM guest:
Ubuntu 22.04.5, x86_64, kernel 5.15.0-198-generic. The host WSL kernel was not
modified. This run performed a clean guest build, loaded the module, ran the
complete integration suite, ran the end-to-end demo, unloaded the module, and
captured a fresh kernel log health scan. It was not a fresh VM provisioning or
cold-boot test.

| Feature | Build | Runtime | Evidence | Status |
|---|---|---|---|---|
| Guest user CLI (`-Wall -Wextra -Wpedantic`) and kernel module (`W=1`) clean build | PASS | N/A | `evidence/build.log` | PASS |
| EDU PCI discovery (`1234:11e8`), probe, MSI setup, character node, and remove | PASS | PASS | `evidence/device_test.txt`, `evidence/device_state_loaded.txt`, `evidence/module_load.txt`, `evidence/module_unload.txt` | VERIFIED |
| INTx fallback path | PASS | NOT RUN | Driver fallback implementation; guest selected MSI | UNVERIFIED |
| BAR0 MMIO inversion | PASS | PASS | `evidence/mmio_test.txt` | VERIFIED |
| Character device, ioctl, blocking/nonblocking read, poll, and checked user-copy errors | PASS | PASS | `evidence/device_test.txt`, `evidence/ring_test.txt` | VERIFIED |
| Factorial result, kernel range rejection, and interrupt completion | PASS | PASS | `evidence/factorial_irq_test.txt`, `evidence/ring_test.txt` | VERIFIED |
| 64-entry event ring FIFO, blocking wakeup, wraparound, and overflow | PASS | PASS | `evidence/ring_test.txt` | VERIFIED |
| DMA signal-interruption cleanup and H2D/D2H at 1, 257, and 4095 bytes | PASS | PASS | `evidence/dma_test.txt` | VERIFIED |
| Zero, invalid-direction, and exact 4096-byte DMA requests rejected before hardware access | PASS | PASS | `evidence/dma_test.txt` | VERIFIED |
| Three idle module reload cycles | PASS | PASS | `evidence/reload_test.txt` | VERIFIED; idle only |
| Full integration suite and end-to-end demo | PASS | PASS | `evidence/full_test.txt`, `evidence/demo_output.txt` | PASS |
| Final module unload and kernel health scan | PASS | PASS | `evidence/module_unload.txt`, `evidence/dmesg_snapshot.txt`, `evidence/kernel_health_check.txt` | PASS |
| Exact 4096-byte transfer behavior in the tested QEMU 6.2.0 implementation | N/A | Historical reproduction: QEMU exits | `evidence/qemu_dma_boundary_failure.txt` | KNOWN EMULATOR LIMITATION |

The final build used the guest's Linux 5.15.0-198-generic headers and GCC
11.4.0. No compiler warnings were emitted. The kernel build skipped BTF
generation because the guest does not provide `vmlinux`; this is recorded in
`evidence/build.log` and did not fail the module build.

The fresh health scan found no `BUG`, `WARNING`, Oops, panic, general protection
fault, use-after-free, KASAN, hung-task, or lockup messages. The kernel log
contains the expected taint/signature notices for loading an unsigned
out-of-tree module; these are not health-check failures.

The EDU specification defines a 4096-byte internal buffer. In the tested QEMU
6.2.0 implementation, an exact-end transfer triggers an out-of-bounds check
and exits the emulator. The original failure remains recorded as historical
evidence; the final fresh suite verified that the driver rejects 4096 before
programming DMA. Transfers of 1, 257, and 4095 bytes passed byte-for-byte H2D
and D2H checks. Therefore the project describes 4095 as the maximum verified
size in this specific emulator setup, not a fundamental EDU limit.

MSI was selected and runtime-tested; the INTx fallback is source-reviewed but
not runtime-tested. Reload evidence covers three idle cycles. Active PCI sysfs
unbind, removal during a blocked read or operation, and the wedged-DMA
quarantine path were not injected or runtime-tested.
