# Verification report

Final cold-start verification completed on 2026-10-06. QEMU ran an Ubuntu 22.04.5 guest with kernel 5.15.0-198-generic. The PCI driver was built and loaded only inside that guest; the WSL host kernel was not modified.

| Feature | Build | Runtime | Evidence | Status |
|---|---|---|---|---|
| Host VM dependencies and QEMU EDU model | PASS | PASS | `evidence/host_environment.txt` | VERIFIED |
| Missing cloud-init host tools are fetched and unpacked project-locally | PASS | PASS | `evidence/host_tool_bootstrap.txt` | VERIFIED |
| VM boot and guest provisioning | PASS | PASS | `evidence/guest_environment.txt` | VERIFIED |
| EDU PCI discovery (`1234:11e8`) | PASS | PASS | `evidence/lspci_before_driver.txt`, `evidence/device_state.txt` | VERIFIED |
| PCI probe, MSI setup, and remove | PASS | PASS | `evidence/module_load.txt`, `evidence/module_unload.txt` | VERIFIED |
| INTx fallback path | PASS | NOT RUN | Driver fallback implementation; guest used MSI | UNVERIFIED |
| BAR0 MMIO inversion | PASS | PASS | `evidence/mmio_test.txt` | VERIFIED |
| Character device, ioctl, read, and poll | PASS | PASS | `evidence/device_test.txt`, `evidence/ring_test.txt`, `evidence/full_test.txt` | VERIFIED |
| Factorial interrupt completion and range checks | PASS | PASS | `evidence/factorial_irq_test.txt` | VERIFIED |
| Event ring FIFO, blocking wakeup, wraparound, and overflow | PASS | PASS | `evidence/ring_test.txt` | VERIFIED |
| DMA host-to-device and device-to-host at 1, 257, and 4095 bytes | PASS | PASS | `evidence/dma_test.txt` | VERIFIED |
| Exact 4096-byte DMA boundary request is rejected before hardware access | PASS | PASS | `evidence/dma_test.txt` | VERIFIED |
| Exact 4096-byte transfer in QEMU 6.2 | PASS | FAIL (QEMU exits) | `evidence/qemu_dma_boundary_failure.txt` | FAILED |
| Module reload (three cycles) | PASS | PASS | `evidence/reload_test.txt` | VERIFIED |
| Guest clean build with warning flags | PASS | PASS | `evidence/build.log` | VERIFIED |
| Kernel health scan | PASS | PASS | `evidence/kernel_health_check.txt`, `evidence/dmesg_driver.txt` | VERIFIED |
| End-to-end demo | PASS | PASS | `evidence/demo_output.txt` | VERIFIED |
| Full integration suite | PASS | PASS | `evidence/full_test.txt` | VERIFIED |

The exact 4096-byte DMA attempt is recorded as a real emulator failure, not a passing feature. QEMU 6.2 rejects a transfer whose end lands exactly on the EDU device buffer boundary and exits. The driver therefore rejects 4096-byte requests with `EINVAL` before programming DMA; verified transfers reach 4095 bytes. See `docs/debugging_notes.md` and `docs/qemu_edu_registers.md` for the source-level explanation.

The health scan found no BUG, WARNING, Oops, panic, KASAN, hung-task, or lockup messages. The kernel log does show the expected taint/signature messages for loading an unsigned out-of-tree module.
