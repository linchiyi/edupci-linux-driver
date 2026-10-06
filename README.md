# EduPCI

**Linux PCI device driver for QEMU EDU virtual hardware**

EduPCI is a small portfolio project that implements and exercises a Linux PCI driver against QEMU's EDU virtual PCI device (PCI ID 1234:11e8). It is designed to make PCI probe/remove, MMIO, interrupt-driven completion, userspace interfaces, ring-buffer synchronization, and DMA concrete and reviewable.

> This project uses emulated hardware in a QEMU Linux guest. It does not claim physical-board bring-up or production driver experience.

## Overview

The host is Ubuntu 22.04.5 under WSL2 and runs QEMU. A separate Ubuntu cloud-image guest receives the EDU PCI device. Kernel compilation and every module load happen inside that guest; the WSL host kernel is not modified.

## Motivation

A small, reproducible target is useful for learning Linux driver APIs without depending on a physical board. QEMU EDU has a published register specification and supports MMIO, IRQ, and DMA paths suitable for focused integration tests.

## What this project demonstrates

- PCI ID matching, probe/remove, BAR0 claiming and mapping
- MMIO access with readl()/writel()
- A misc character device with fixed-width ioctl ABI
- Interrupt-driven factorial and DMA completion using completions
- A bounded IRQ-to-userspace event ring with blocking read() and poll()
- DMA mask setup and coherent DMA transfers in both directions
- Error cleanup, timeouts, repeated module load/unload, and guest evidence collection

Runtime status for each feature is tracked in docs/verification_report.md; only verified items should be claimed in an interview.

## Architecture

~~~mermaid
flowchart LR
  WSL[WSL2 Ubuntu host] --> QEMU[QEMU q35 + KVM]
  QEMU --> Guest[Ubuntu Linux guest]
  Guest --> PCI[QEMU EDU PCI 1234:11e8]
  Guest --> Driver[EduPCI kernel module]
  Driver --> BAR[BAR0 MMIO]
  PCI --> IRQ[MSI or INTx]
  IRQ --> Driver
  Driver --> Node[/dev/edupci]
  CLI[edu_cli] --> Node
  IRQ --> Ring[64-event ring]
  CLI --> Ring
  Driver <--> DMA[Coherent DMA buffer]
  DMA <--> PCI
~~~

## Environment

- Host: Ubuntu 22.04.5 LTS on WSL2, x86_64, QEMU 6.2.0, GCC 11.4, Make 4.3, KVM available.
- Guest: Ubuntu 22.04 amd64 cloud image; exact kernel/toolchain and image checksum are captured in evidence/guest_environment.txt after provisioning.
- The QEMU EDU register subset and source references are documented in docs/qemu_edu_registers.md.

## Quick demo

Host requirements are QEMU with KVM access, `qemu-img`, SSH tools, `curl`, and Ubuntu's `apt-get` / `dpkg-deb`. If `cloud-localds` or `genisoimage` is missing, `make vm` downloads the Ubuntu packages and unpacks them under ignored `.vm/host-tools`; it does not install packages system-wide.

~~~sh
make provision
make demo
~~~

The first command creates and boots the VM, sets up SSH, and installs guest headers/tools. Later runs can use `make demo` directly while the VM is running. The demo output is saved to `evidence/demo_output.txt`.

## Features

The driver exposes:

- info: PCI identity, EDU version, BAR size, selected IRQ mode
- invert: QEMU EDU liveness register
- factorial: interrupt-driven calculation for inputs 0 through 12
- events: blocking event read with a bounded CLI wait
- stats: IRQ/completion/overflow counters
- ring-test: blocking read wakeup, empty, poll, FIFO, wraparound, full and overflow behavior
- dma-test: H2D and D2H byte comparisons at 1, 257 and 4095 bytes
- selftest: combined smoke test

The supported register subset is summarized in docs/qemu_edu_registers.md.

## Build and run

~~~sh
make vm
make provision
make build
make load
make unload
make stop
~~~

All driver runtime commands are executed by host scripts through SSH inside the guest. The guest source tree is ~/edupci. make build cleans and rebuilds the kernel module and CLI in the guest.

## Testing

~~~sh
make test
~~~

The end-to-end suite starts the guest if needed, syncs sources, performs a clean guest build, loads the driver, runs PCI/MMIO/factorial/IRQ/ring/DMA/reload tests with bounded waits, captures kernel logs, and returns nonzero on failure. Per-test scripts are under tests/.

## Project structure

- driver/edu_main.c — PCI lifecycle, MMIO, IRQ, misc device, event ring, DMA
- include/edu_ioctl.h — fixed-width user/kernel ABI
- user/edu_cli.c — CLI and actual-content checks
- scripts/ — VM lifecycle, provisioning, source sync, build and evidence
- tests/ — guest integration tests
- docs/ — architecture, register map, learning and interview guides
- evidence/ — captured environment and runtime output

## Implementation highlights

The driver serializes access to EDU's shared operation registers. A completion links a blocking ioctl to its hardware interrupt. The IRQ handler stays short and non-sleeping. A spinlock protects event ring state shared with read(); a mutex protects process-context hardware operations. Each wait is bounded.

## Concurrency model

The ring uses a 64-record capacity. IRQ is the producer and read() is the consumer. When full, the driver drops the newest event and increments an overflow counter. The full protection table and timing diagram are in docs/concurrency_model.md.

## DMA

The driver sets a 28-bit DMA mask and uses dma_alloc_coherent() for the host buffer. It keeps the CPU virtual pointer separate from the DMA address, requests an IRQ on completion, and verifies actual bytes in both directions. EDU's internal buffer is 4096 bytes; this project caps transfers at 4095 because QEMU 6.2 rejects the exact-end range due to an off-by-one check documented in docs/qemu_edu_registers.md.

## Debugging lessons

Real issues and their evidence are kept in docs/debugging_notes.md. The project avoids fabricated benchmark claims; correctness checks take priority over throughput claims.

## Verification

See docs/verification_report.md and the captured output under evidence/. A source file or successful compile alone is not counted as runtime verification.

## Limitations

- Target hardware is QEMU EDU, not a physical PCIe device.
- One EDU function is supported at a time by the misc-device interface.
- Factorial inputs are limited to 0..12 to fit the unsigned 32-bit result.
- DMA transfers are capped at 4095 bytes to avoid QEMU 6.2's exact-end boundary-check failure.
- The guest uses QEMU/KVM behavior; it does not test real-board electrical behavior, firmware integration, production IOMMU configurations, or hardware-specific errata.

## What I learned

The project connects the PCI probe lifecycle to the userspace ABI, and shows why execution context determines synchronization choices. It also makes the distinction between CPU pointers and DMA addresses, and the ordering of interrupt acknowledgement, completion, and resource cleanup visible in code and test output.

## Interview topics

Start with PCI probe and BAR/MMIO, then user/kernel boundaries and ioctl, process versus IRQ context, completion and wait queues, mutex versus spinlock, ring overflow policy, DMA addressing, error unwinding, and the project's QEMU-only limits. The Traditional Chinese guides provide repository-specific answers.
