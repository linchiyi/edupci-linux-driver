# Debugging notes

This file records issues observed during implementation. It does not invent failures to fill a quota.

## 1. Userspace poll readiness did not compile

- **Symptom:** The host C compiler rejected POLLRDNORM as undeclared while building user/edu_cli.c with -std=c11 -Wall -Wextra -Wpedantic.
- **Initial hypothesis:** A missing header or feature-test macro in the userspace poll API.
- **Evidence:** poll.h was included; the compiler reported POLLRDNORM undeclared at event_readable().
- **Root cause:** POLLRDNORM is not exposed by the selected strict C feature set on this Ubuntu build, while POLLIN is the portable userspace readiness bit. The driver still reports its readable mask using the kernel's EPOLLIN/EPOLLRDNORM flags.
- **Fix:** The CLI checks POLLIN only.
- **Verification:** make -C user clean all completed without warnings on the host compiler.
- **Lesson:** Kernel and userspace use related but not identical poll flag interfaces; compile under the promised warning/standard flags.

## 2. VM port selection could wait indefinitely

- **Symptom:** The first vm_setup.sh run did not produce a guest image or seed and remained active before image download.
- **Initial hypothesis:** SSH key generation or network download was stuck.
- **Evidence:** Process inspection showed the shell waiting for nc -z 127.0.0.1 2222; no curl process had started. Host ss output had already shown that port 2222 was free.
- **Root cause:** This netcat build did not bound a failed connect attempt without a timeout option.
- **Fix:** Port checks now use nc -z -w 1 and continue through the documented local port range.
- **Verification:** A rerun passed port selection and started downloading the official cloud image.
- **Lesson:** Every connectivity probe in an automation script needs a finite timeout.

## 3. Host cloud-init helper was unavailable

- **Symptom:** The initial host audit found cloud-localds and genisoimage were not installed system-wide.
- **Initial hypothesis:** The VM could not use the normal cloud-init seed workflow without a host package change.
- **Evidence:** command -v reported both missing; the normal sudo install request reported that an interactive password was required. Ubuntu Jammy package metadata identified cloud-image-utils 0.32-22-g45fe84a5-0ubuntu1 and genisoimage 9:1.1.11-3.2ubuntu1.
- **Root cause:** The minimal WSL host did not include cloud-image-utils or its ISO writer.
- **Fix:** `vm_setup.sh` now downloads those Ubuntu packages with `apt-get download` and unpacks them into ignored `.vm/host-tools`, avoiding a host-wide package change. The helper then creates the seed ISO.
- **Verification:** Moved the existing project-local helper directory aside and ran `make vm`; the packages were fetched, both commands became available from the extracted directory, and the seed ISO was created. Output is in `evidence/host_tool_bootstrap.txt`; the guest booted and accepted the cloud-init SSH key.
- **Lesson:** A reversible project-local dependency can keep VM setup reproducible when privileged host package installation is unavailable.

## 4. QEMU 6.2 exited on an exact 4096-byte EDU DMA transfer

- **Symptom:** PCI, MMIO, factorial IRQ, and ring tests passed, then the QEMU process exited during dma-test and SSH closed.
- **Initial hypothesis:** The driver might have programmed a bad DMA address or reversed the H2D/D2H direction.
- **Evidence:** The first line of .vm/logs/qemu.log reported `EDU: DMA range 0x40000-0x40fff out of bounds (0x40000-0x40fff)`. The coherent DMA address was below the configured 28-bit mask. QEMU v6.2.0 hw/misc/edu.c uses an end-exclusive within() check, then tests end1 as if it had to be strictly inside the buffer.
- **Root cause:** A transfer ending exactly at the 4096-byte device-buffer boundary is rejected by QEMU's range-check implementation, despite the register specification describing a 4096-byte internal buffer. QEMU terminates the whole emulator on this check.
- **Fix:** The driver now caps a transfer at 4095 bytes and rejects length 4096 with EINVAL before programming the device. The CLI tests 1, 257, and 4095 bytes in both directions and explicitly checks that the exact-boundary request is rejected.
- **Verification:** After the cold guest restart, the full suite passed; the 4096-byte request was rejected before DMA, and 1/257/4095-byte H2D and D2H transfers passed. The original emulator failure remains captured in evidence/qemu_dma_boundary_failure.txt.
- **Lesson:** A device specification and a specific emulator implementation can disagree at boundary conditions. Read the version-matched implementation and test the boundary in a recoverable VM.

## 5. Probe evidence needed privileged dmesg access

- **Symptom:** make load showed a permission error from dmesg even though insmod, lsmod, and the character-node check succeeded; a separate vm_ssh.sh check failed with “vm_ssh: not found”.
- **Initial hypothesis:** The driver might have failed after module insertion, or the SSH helper might not be using the project key.
- **Evidence:** A direct SSH inspection showed /dev/edupci, the PCI driver symlink, the probe log, and IRQ=MSI. The guest applies kernel.dmesg_restrict; vm_ssh was a shell function and exec cannot launch a function as a separate program.
- **Root cause:** The evidence script used unprivileged dmesg and the wrapper incorrectly used exec with a local function.
- **Fix:** The scripts now run sudo dmesg with pipefail, and vm_ssh.sh invokes the function in the current shell.
- **Verification:** make load captured probe output and the independent wrapper displayed device info successfully.
- **Lesson:** A shell pipeline can hide a failing left-hand command, and guest kernel logs may require root.

## 6. Strict POSIX userspace build hid kill()

- **Symptom:** The blocking-read test compiled but -Wall reported an implicit declaration of kill().
- **Initial hypothesis:** signal.h was missing.
- **Evidence:** signal.h was present; the build uses -std=c11 and the compiler hid this POSIX declaration without a feature-test macro.
- **Root cause:** Strict C mode limits non-ISO declarations unless the requested POSIX API level is declared before headers.
- **Fix:** Define _POSIX_C_SOURCE 200809L before the system headers.
- **Verification:** make -C user clean all completed with no warnings.
- **Lesson:** Keep feature-test macros consistent with the POSIX APIs used by a CLI.
