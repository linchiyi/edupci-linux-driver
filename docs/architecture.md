# Architecture

## Scope

EduPCI is a Linux kernel module and userspace CLI for the QEMU EDU virtual PCI device (PCI ID 1234:11e8). It demonstrates Linux PCI binding, BAR0/MMIO, a misc character device, IRQ-driven completion, a bounded event ring, blocking reads, poll readiness, and coherent DMA. It is not a driver for a physical board.

The WSL2 host runs QEMU and edits/builds project tooling. All kernel builds and module loading happen inside the Ubuntu guest. The host WSL kernel is never modified by this project.

## Components

~~~mermaid
flowchart LR
    CLI[edu_cli] -->|ioctl, read, poll| NODE[/dev/edupci]
    NODE --> DRIVER[EduPCI kernel module]
    DRIVER -->|PCI probe/remove| PCI[QEMU EDU PCI 1234:11e8]
    DRIVER -->|readl/writel| BAR[BAR0 MMIO]
    PCI -->|MSI or INTx| IRQ[short IRQ handler]
    IRQ -->|completion| DRIVER
    IRQ -->|event push| RING[64-record ring buffer]
    CLI -->|blocking read/poll| RING
    DRIVER -->|DMA API address| DMA[coherent host buffer]
    DMA <-->|EDU DMA engine| PCI
~~~

## Request flows

### Factorial

1. edu_cli factorial N passes a fixed-width struct edu_factorial using EDU_IOC_FACTORIAL.
2. edu_do_factorial() validates N <= 12, serializes access with operation_lock, clears stale IRQ causes, reinitializes the completion, enables EDU's factorial IRQ, and writes N to BAR0 offset 0x08.
3. The caller sleeps in wait_for_completion_interruptible_timeout().
4. QEMU finishes the operation and raises cause 0x01. edu_irq_handler() reads and acknowledges the cause, reads the result, pushes a ring event, and completes the request.
5. The waiting task wakes, reads the result register, disables factorial IRQ generation, releases the mutex, and copies the result back to userspace.

### Event stream

The hard IRQ handler is the ring producer. It assigns a monotonic sequence number and enqueues a fixed-size event while holding ring_lock with IRQ save/restore. When full, it drops the new event and increments event_overflows. read() is the process-context consumer. It sleeps on read_wait while empty; poll() reports readable when records are available.

### DMA

edu_do_dma() copies host-to-device input into a dma_alloc_coherent() buffer, executes dma_wmb(), programs the EDU DMA source/destination/count/command registers, then sleeps on dma_done. The IRQ handler acknowledges cause 0x100 and completes the wait. For device-to-host, the driver executes dma_rmb() and copies the coherent buffer to the caller. The CLI compares bytes for 1, 257, and 4096-byte transfers in both directions.

## Resource lifetime

edu_probe() enables the PCI function, requests its BAR regions, maps BAR0, checks identification, sets a 28-bit DMA mask, allocates the coherent buffer, enables bus mastering, allocates MSI (with INTx fallback), requests the IRQ, and registers /dev/edupci. Every failure jumps through reverse-order cleanup.

edu_remove() marks the instance dead, deregisters the misc device, wakes blocked readers and operation completions, waits for the serialized operation path to leave, disables device IRQ generation, acknowledges pending causes, frees the IRQ, clears bus mastering, frees DMA memory, unmaps BAR0, releases regions, and disables the PCI function. Open files hold a kref; after removal they see ENODEV or EOF rather than dereferencing released PCI resources.

