# Concurrency model

| Shared state | Process context | IRQ context | Protection |
|---|---|---|---|
| EDU factorial/DMA registers | ioctl path programs and reads registers | handler reads IRQ status and completion value | operation_lock serializes process requests; the handler never takes this mutex. Register roles and operation sequencing are documented in qemu_edu_registers.md. |
| active_operation | ioctl sets and clears the active request | handler reads it before matching a cause | WRITE_ONCE / READ_ONCE. It is a small discriminator; operation_lock ensures only one process operation is active. |
| engine_faulted | operation path sets if an EDU engine does not become idle after timeout | not read by handler | operation_lock; failed hardware is not reused for a later request. |
| factorial_done / dma_done | waiting ioctl reinitializes and waits | matching handler calls complete() | Linux completion primitive supplies its own synchronization. Reinitialize before issuing the device command. |
| ring, head, tail, count, last_sequence, event_overflows | read() snapshots/commits entries; stats ioctl reads counters | handler appends entries | ring_lock, acquired with spin_lock_irqsave() in both contexts. Copy to userspace occurs after releasing it; tail/count commit only follows a successful copy. |
| read_wait | read()/poll() wait for records | event producer wakes readers | wait-queue API; ring_lock remains the authority on whether data exists. |
| coherent DMA buffer | ioctl copies user data and starts transfer | handler only records completion; EDU accesses DMA memory | operation_lock serializes userspace access. dma_wmb() before device reads host data; dma_rmb() after device writes host data. Do not inspect a transfer before completion. |
| irq_count, factorial_count, dma_count | stats ioctl reads | handler increments | atomic64_t; these are independent counters, not compound state. |
| dead flag | remove writes; read/ioctl checks | handler does not need it to acknowledge a cause | WRITE_ONCE / READ_ONCE. remove keeps BAR/IRQ state valid until operations exit and free_irq() completes. |
| singleton misc-device pointer | probe/remove publish or clear; open acquires a kref | not used | edu_singleton_lock mutex. Open files hold a kref after open succeeds. |
| read_lock | read() serializes consumers | not used | mutex. It may sleep; never acquired in IRQ context. |

## Primitive choices

- operation_lock is a mutex because ioctl operations run in process context and may sleep while waiting for completion or copying user memory. It also prevents concurrent callers from overwriting the single EDU factorial and DMA register sets.
- ring_lock is a spinlock with IRQ save/restore because the ring is shared by hard IRQ producer and process-context consumer. The consumer must avoid deadlocking if interrupted while holding the same lock.
- No mutex is taken by edu_irq_handler(). It only reads/acks MMIO, updates atomics, briefly appends to the ring, wakes the read queue, and completes the active request.
- Atomic counters are used only for independent statistics. They do not replace the ring lock or operation mutex.
- The completion means “this particular serialized hardware request finished.” The wait is bounded to 3 seconds; a lost interrupt becomes an error instead of an unbounded hang.

## Race example

Without ring_lock, an IRQ could update ring_head and ring_count between a process reader's empty check and tail update. The reader might consume a partially written record, lose a newly queued event, or make the producer believe a full ring has room. Both insertion and removal therefore make the record and indices visible as one protected update.

## Factorial timing

~~~text
userspace task       driver/process context      EDU device       hard IRQ
     | ioctl factorial     |                          |                |
     |-------------------->| mutex; reinit completion |                |
     |                     | enable IRQ; write N ---->|                |
     |                     | wait (task sleeps)       | calculate      |
     |                     |                          |---- IRQ ------>|
     |                     |                          |   read + ack   |
     |                     |                          |   ring push    |
     |                     |<------ complete() -----------------------|
     | wakes; result <-----| read result; unlock      |                |
~~~

## Removal

remove first marks the instance dead and deregisters the misc device. It wakes blocked reads and completes operation waits so that open callers can exit. It then takes operation_lock before touching or releasing BAR/DMA resources. free_irq() synchronizes with a handler already in progress before BAR0 is unmapped.

Plain `rmmod` is prevented while an open file exists because the file operations
set `.owner = THIS_MODULE`. PCI sysfs unbind is separate and has not been
runtime-tested while read or hardware work is active. If a DMA engine does not
clear RUN within the remove timeout, the driver disables bus mastering and
INTx and intentionally retains its coherent allocation; this avoids freeing
memory that a still-active EDU engine may address, at the cost of a rare
allocation leak. The QEMU runtime suite covers only idle reloads.
