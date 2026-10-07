# EduPCI independent audit

Initial audit performed before fixes on 2026-10-06. The source, tests, scripts, tracked
evidence, verification report, and Git history were reviewed. The device
register map was checked against the QEMU v6.2.0 EDU specification and
`hw/misc/edu.c`. Findings below describe the pre-fix tree; dispositions are
updated after the fixes and fresh verification.

## Initial severity counts

| Severity | Count |
|---|---:|
| BLOCKER | 0 |
| HIGH | 2 |
| MEDIUM | 4 |
| LOW | 3 |
| INFO | 3 |

## Findings

### HIGH-1 — An interrupted operation can leave a stale IRQ for the next request

- **File/function:** `driver/edu_main.c`, `edu_do_factorial()` and
  `edu_do_dma()` (completion error paths).
- **Problem:** Both functions clear `active_operation` and acknowledge pending
  causes immediately after an interruptible completion wait returns due to a
  signal or timeout. They only poll the hardware idle bit after clearing the
  active operation. A completion cause that arrives after the early
  acknowledgement can be handled after the next request has set the same
  `active_operation`, completing the wrong request. QEMU EDU's factorial worker
  clears COMPUTING before raising its IRQ; its DMA timer clears RUN before
  raising the DMA IRQ, so cleanup must drain the completion before rearming.
- **Why it matters:** The ioctl can report a stale factorial result or DMA
  success before the new DMA has completed. Happy-path tests do not exercise
  signal or timeout cleanup.
- **Evidence:** Source ordering at `edu_main.c:237-256` and `313-329`; QEMU
  v6.2.0 `hw/misc/edu.c` has the corresponding status-clear/IRQ-raise order.
  `test_factorial.sh` and `test_dma.sh` only exercise successful waits.
- **Recommended fix:** Keep the operation marked active while draining a
  possibly late completion, wait for hardware quiescence, synchronize the IRQ,
  and acknowledge any final cause before making the operation reusable. If
  completion cannot be safely drained, prevent another request from reusing
  that engine until recovery.

### HIGH-2 — Remove proceeds to free DMA resources after a failed idle wait

- **File/function:** `driver/edu_main.c`, `edu_remove()` and
  `edu_free_resources()`.
- **Problem:** If `EDU_DMA_RUN` remains set after the three-second remove wait,
  remove logs a warning and continues. Cleanup eventually frees the IRQ/vector,
  clears bus mastering, and frees the coherent allocation without proving the
  device stopped using it or that pending transactions drained.
- **Why it matters:** A wedged device could DMA into memory after
  `dma_free_coherent()`, or raise an interrupt after IRQ teardown. The current
  tests only unload while idle, so they do not validate this failure case.
- **Evidence:** `edu_remove()` at `edu_main.c:754-766` ignores the timeout result
  after logging; `edu_free_resources()` at `edu_main.c:585-615` then tears down
  IRQ, DMA, and BAR resources. QEMU EDU exposes no DMA cancel register.
- **Recommended fix:** Make teardown stop new bus mastering before IRQ/DMA
  release and do not free the coherent buffer unless the device is known to be
  quiescent. If the EDU engine cannot be cancelled or proven idle, explicitly
  document the unbind limitation and retain the buffer rather than risking
  DMA-after-free. Test only a controlled, supported unbind path.

### MEDIUM-1 — A failed userspace copy consumes ring events without accounting

- **File/function:** `driver/edu_main.c`, `edu_read()`.
- **Problem:** Entries are removed from the ring at `edu_main.c:415-423` before
  `copy_to_user()` at lines 425-427. An invalid or partially inaccessible user
  buffer returns `-EFAULT` after the records have already been discarded; the
  overflow counter does not record that loss.
- **Why it matters:** A caller can lose queued events due to a transient or
  mistaken user pointer even though `read()` reports failure.
- **Evidence:** The dequeue precedes the user copy in the function. The current
  walkthrough also acknowledges the loss at `docs/implementation_walkthrough.md`.
- **Recommended fix:** Snapshot entries while holding `ring_lock`, copy outside
  the spinlock, and commit the tail/count update only after a successful copy.
  `read_lock` already serializes consumers, and the producer never overwrites
  queued records.

### MEDIUM-2 — One document claims a 4096-byte transfer was tested

- **File/function:** `docs/architecture.md`, DMA description; also
  `docs/resume_bullets.md`, Chinese wording.
- **Problem:** Architecture says the CLI compares 4096-byte transfers, but the
  test uses 1, 257, and 4095 bytes and rejects 4096 before MMIO. The phrase
  “實機制式驗證” in the Chinese resume bullet can also be read as real-machine
  hardware verification even though the evidence is from a QEMU guest.
- **Why it matters:** These statements overstate both the tested DMA length and
  the hardware environment.
- **Evidence:** `user/edu_cli.c:548-562`, `evidence/dma_test.txt`, and the
  QEMU-specific guard in `include/edu_ioctl.h`.
- **Recommended fix:** State 4095 bytes as the largest verified transfer in
  this QEMU 6.2 environment, explicitly keep the 4096-byte EDU specification
  buffer separate from the emulator limitation, and call the result QEMU guest
  runtime verification.

### MEDIUM-3 — Factorial range test stops in userspace

- **File/function:** `tests/test_factorial.sh`, `user/edu_cli.c`.
- **Problem:** The test invokes `edu_cli factorial 13`, but the CLI rejects
  values above 12 before issuing `EDU_IOC_FACTORIAL`. It therefore does not
  verify the driver's `-ERANGE` path, despite the report grouping range checks
  with the kernel factorial operation.
- **Why it matters:** A broken kernel range check could still pass this test.
- **Evidence:** `user/edu_cli.c:140-145` performs the range check before
  `factorial_fd()`; the driver check is at `edu_main.c:216-217`.
- **Recommended fix:** Add a direct UAPI test for the kernel's `-ERANGE`, or
  describe the existing check accurately as CLI-side range rejection.

### MEDIUM-4 — Active remove/unbind behavior is not runtime-verified

- **File/function:** `tests/test_reload.sh`; `edu_remove()`.
- **Problem:** The reload test performs three idle `rmmod`/`insmod` cycles. It
  does not remove/unbind the PCI function while a blocking reader, factorial,
  or DMA request is active.
- **Why it matters:** The dead flag, wakeups, completion abort, and operation
  teardown are source-reviewed but not covered by the reported reload evidence.
- **Evidence:** `tests/test_reload.sh` contains no concurrent process or
  sysfs-unbind path; `evidence/reload_test.txt` records only idle cycles.
- **Recommended fix:** Keep the claim limited to idle module reloads. State that
  active PCI unbind/removal is unverified unless a safe, controlled test is
  added. An open file pins the module through `.owner = THIS_MODULE`, so plain
  `rmmod` cannot test removal while that file is blocked.

### LOW-1 — CLI accepts a negative zero as an unsigned value

- **File/function:** `user/edu_cli.c`, `parse_u32()`.
- **Problem:** `strtoul()` accepts a leading minus sign. On the current 64-bit
  guest most negative values overflow the accepted `u32` range, but `-0` parses
  as zero and is accepted; behavior can also vary with `unsigned long` width.
- **Why it matters:** The CLI's unsigned integer syntax is not consistent across
  architectures and malformed input is not rejected uniformly.
- **Evidence:** `parse_u32()` at `user/edu_cli.c:44-54` does not reject a leading
  `-`.
- **Recommended fix:** Reject a minus sign before calling `strtoul()` and add a
  small parser regression check.

### LOW-2 — Evidence collection can turn a dmesg failure into an empty PASS file

- **File/function:** `scripts/collect_evidence.sh`.
- **Problem:** Both dmesg pipelines end in `|| true`, so a failed `sudo dmesg`
  can be indistinguishable from a clean scan. An empty
  `evidence/kernel_health_check.txt` does not state which occurred.
- **Why it matters:** The health result is less auditable than the other
  evidence, even though the existing driver log contains successful dmesg data.
- **Evidence:** `scripts/collect_evidence.sh:7-10`; the tracked health file is
  empty while `evidence/dmesg_driver.txt` has entries.
- **Recommended fix:** Capture `sudo dmesg` directly with failure propagation,
  scan that captured snapshot, and record an explicit no-matches result.

### LOW-3 — VM setup discards its local package cache and ignores unpacked tools

- **File/function:** `scripts/vm_setup.sh`, `ensure_cloud_image_tools()`.
- **Problem:** Setup deleted all downloaded `.deb` files before every attempted
  fetch and did not add the existing `.vm/host-tools/usr/bin` directory to
  PATH. A repeated VM start therefore required DNS/network even when the helper
  tools had already been unpacked locally.
- **Why it matters:** The documented project-local dependency fallback was not
  repeatable in a restricted or temporarily offline environment.
- **Evidence:** The first audit test run failed resolving `archive.ubuntu.com`
  after this script deleted its cache; `.vm/host-packages` and the unpacked
  helper directory existed.
- **Recommended fix:** Reuse the unpacked helpers and preserve cached package
  files before trying a network download.

## Resource lifetime table

| Resource/state | Acquired or published | Probe failure cleanup | Remove cleanup | Assessment |
|---|---|---|---|---|
| Singleton claim | Set under `edu_singleton_lock` before allocation | `err_claim` clears it | Cleared after resource teardown | Balanced on inspected paths |
| `edu_device` allocation and binding kref | `kzalloc()`, then `kref_init()` | `err_put` drops the initial ref | Dropped after resource teardown; open files retain their own refs | No stale file pointer found |
| PCI function enable | `pci_enable_device()`; flag set only on success | `pci_disable_device()` when flagged | Same | Balanced |
| BAR regions | `pci_request_regions()`; flag after success | `pci_release_regions()` when flagged | Same | Balanced |
| BAR0 mapping | `pci_iomap()`; non-NULL pointer indicates acquisition | `pci_iounmap()` when mapped | `free_irq()` happens before unmap | Balanced |
| DMA mask | `dma_set_mask_and_coherent(28)` | No release API required | No release API required | Matches EDU's documented 28-bit mask |
| Coherent buffer | `dma_alloc_coherent()`; non-NULL pointer indicates acquisition | `dma_free_coherent()` when allocated | Freed after normal quiescence; retained if RUN does not clear | Normal path balanced; quarantine path source-reviewed, not fault-injected |
| Bus master | `pci_set_master()`; flag set after call | `pci_clear_master()` when flagged | Cleared before IRQ and DMA teardown | Stops new bus-master requests before freeing DMA memory; if quiescence cannot be established the coherent buffer is retained |
| IRQ vector allocation | `pci_alloc_irq_vectors()` returns one; flag set | `pci_free_irq_vectors()` when flagged | Same | Failure after vector allocation is unwound |
| IRQ handler | `request_irq()`; flag after success | `free_irq()` when flagged | Freed before BAR unmap | Correct handler/BAR lifetime on the normal path |
| Misc node / singleton pointer | Pointer set and `misc_register()` under singleton mutex | Pointer cleared if registration fails | `misc_deregister()` and pointer clear under mutex | Balanced; no later probe failure after successful registration |
| PCI drvdata | Set only after successful misc registration | Not set on earlier failures | Cleared during remove | Balanced |

Probe errors before `pci_enable_device()` go directly to object/claim release.
After enable succeeds, all later failures use flag-checked cleanup. MSI failure
falls back to one legacy vector; failures after vector allocation free the
vector, and failures after `request_irq()` free the handler before unmapping
BAR0. If remove cannot establish DMA quiescence, it clears bus mastering and
intentionally retains the coherent allocation. That exceptional quarantine
path is source-reviewed but was not runtime fault-injected.

## Hardware and synchronization conclusions

- **MMIO/specification:** QEMU EDU v6.2.0 specifies a 1 MiB memory BAR, 4-byte
  accesses below 0x80, 4- or 8-byte accesses from 0x80 onward, inversion at
  0x04, factorial at 0x08, status at 0x20, IRQ status/ack at 0x24/0x64, and DMA
  registers at 0x80-0x98. The driver uses `readl()`/`writel()` throughout and
  checks the BAR extends through register 0x98. The CLI compares the liveness
  result against `~value`; the existing MMIO evidence shows four successful
  patterns.
- **IRQ:** Probe allocates one MSI vector first, falls back to legacy INTx,
  obtains the vector with `pci_irq_vector()`, and requests that exact IRQ.
  `edu_irq_handler()` returns `IRQ_NONE` when IRQ status is zero, reads and
  writes back the cause to the required 0x64 acknowledgement register even for
  MSI, then records/completes recognized factorial or DMA causes. It does not
  sleep or take a mutex. Fresh probe evidence reports MSI IRQ 34; the INTx
  fallback remains source-reviewed and untested at runtime.
- **Normal factorial/DMA completion:** Both paths set the active operation,
  start the device, and wait on `wait_for_completion_interruptible_timeout()`;
  successful return requires the handler to call the corresponding
  `complete()`. Polling loops exist only for pre-operation idle checks and
  timeout/remove cleanup. They do not replace the normal interrupt completion
  path. On interrupted or timed-out waits, cleanup keeps the operation active
  while draining completion or checking idle, synchronizes the IRQ, acknowledges
  pending causes, then permits reuse. If the engine cannot be quiesced, later
  operations fail with `-EIO`. The DMA signal cleanup is runtime-tested; the
  factorial signal path is source-reviewed.
- **Wait queues/poll:** Blocking read tests the condition through
  `wait_event_interruptible_timeout()`. `.poll` registers with `poll_wait()`
  before checking the same locked ring-count readiness used by `read()`, so
  registration cannot lose a wakeup. A later competing reader may consume an
  event after poll reports ready; that is the normal advisory readiness race.
- **Ring:** Capacity is `EDU_EVENT_CAPACITY == 64`; `ring_count` distinguishes
  full from empty; modulo head/tail arithmetic stays in range; full insertion
  drops the new event and increments the overflow counter. IRQ is the producer,
  process-context `read()` is the consumer. Every ring access uses
  `spin_lock_irqsave()`/`spin_unlock_irqrestore()` including readiness and
  stats; this is appropriate for process context sharing the lock with a hard
  IRQ. The lock provides ordering, no call sleeps under it, and user copies
  happen after unlocking.
- **DMA:** Probe sets a 28-bit coherent DMA mask, allocates a 4096-byte coherent
  host buffer, and retains separate CPU and `dma_addr_t` addresses. H2D copies
  a host pattern into coherent memory, uses it as the DMA source, and targets
  EDU offset 0x40000. D2H sources offset 0x40000 and writes into coherent
  memory, then copies to a distinct userspace destination. The CLI compares
  that destination byte-for-byte with the H2D source for 1, 257, and 4095
  bytes. Completion requests an IRQ and the ioctl waits for it; a missing IRQ
  times out. The test does not merely compare an untouched host buffer.
- **4096-byte boundary:** QEMU's v6.2.0 specification says the EDU internal
  buffer is 4096 bytes. The v6.2.0 implementation's `within()` treats the end
  as exclusive while `edu_check_range()` requires `end1` to be strictly within
  the range, so an exact-end transfer triggers `hw_error()`. The project's
  4095-byte cap and pre-MMIO `-EINVAL` guard are appropriately
  version-specific; this is not a fundamental EDU API limit.
- **ABI/errors:** UAPI uses fixed-width `__u32`/`__aligned_u64` fields and
  encoded ioctl sizes/directions; the DMA user pointer is a 64-bit user value,
  never a kernel pointer. `compat_ptr_ioctl` is appropriate for the fixed
  layout. User copies are checked. Driver paths return conventional
  `-EFAULT`, `-EINVAL`, `-ERANGE`, `-ENOTTY`, `-EAGAIN`, `-ETIMEDOUT`,
  `-ERESTARTSYS`, and `-ENODEV` as applicable. Kernel factorial range
  rejection is now tested through the UAPI; the CLI's unsigned parser rejects
  negative values and is covered by regression checks.

## Lock and context table

| Lock | Acquirers | Context | Protects | Can sleep? | Nesting/order |
|---|---|---|---|---|---|
| `edu_singleton_lock` | probe, remove, open | Process | `edu_claimed` and `edu_singleton`; open takes a kref while published | Yes, mutex | Not nested with `operation_lock`; remove releases it before waiting on operations |
| `operation_lock` | hardware ioctls, `edu_remove()` | Process | Shared EDU operation registers, one active request, DMA buffer ownership, teardown exclusion | Yes, mutex; held over completion waits and user copies in DMA | IRQ handler never takes it; no spinlock is held while sleeping |
| `read_lock` | `edu_read()` | Process | Serializes multiple consumers | Yes; held during blocking wait and user copy | May nest before `ring_lock`; no reverse acquisition path |
| `ring_lock` | event producer, read, stats, readiness | Hard IRQ and process | Ring records/indices/count, sequence and overflow stats | No, spinlock | Always acquired with IRQ save/restore; no sleeping or user copy inside |
| Completion internals | waiting ioctl and matching IRQ | Process/IRQ | One factorial or DMA completion | Waiter sleeps; `complete()` does not sleep | Reinitialized before command; error cleanup drains or faults the engine before reuse |

No IRQ/process lock inversion, sleeping under a spinlock, user copy under a
spinlock, or mutex dependency from the IRQ handler was found. The event ring's
locking and poll/read condition ordering are sound. `read_lock` intentionally
serializes readers, so one blocked reader can hold off another reader until an
event, signal, timeout, or removal wakeup.

## Claim-to-test-to-evidence map

| Claim | Test and API exercised | Evidence | Audit result |
|---|---|---|---|
| PCI discovery, probe, misc node | `test_device.sh`: `lspci`, sysfs driver link, char node | `device_test.txt`, `module_load.txt` | Real guest PCI binding; QEMU EDU, not physical PCIe hardware |
| BAR0 liveness/inversion | `test_mmio.sh` → ioctl → `writel/readl` | `mmio_test.txt` | Actual expected `~value` comparison |
| Factorial IRQ completion | `test_factorial.sh` → ioctl blocks for completion; counter/result checked | `factorial_irq_test.txt`, `ring_test.txt`, `module_load.txt` | IRQ path is required for success; MSI shown in probe log. `ring-test` sends input 13 through UAPI and requires kernel `ERANGE` |
| Ring FIFO and poll | `test_events.sh` → real factorial IRQ events, `poll()`, `read()` | `ring_test.txt` | Public path; payload/order checked |
| Ring blocking read | Forked process blocks in `read()`; a factorial ioctl generates a real event | `ring_test.txt` | Exercises kernel blocking wait and wake; no userspace event fabrication |
| Ring wraparound | At least 56 real events precede a 69-event fill; head/tail are modulo 64 | `ring_test.txt` | Operations cross the actual indices; not an internal-state test |
| Ring full/overflow | 69 serialized factorial operations without draining; assert 5 overflow delta and read retained 64 | `ring_test.txt` | Genuine IRQ producer overflow; no diagnostic injection ioctl |
| DMA H2D and D2H | `dma-test` ioctl; distinct source/destination buffers and `memcmp`; interrupted H2D followed by D2H | `dma_test.txt` | Both directions and IRQ completion required; byte transfer and interrupted-wait cleanup validated |
| 4096 guard | `dma-test` submits length 4096 and requires `EINVAL` | `dma_test.txt` | Rejected before operation lock/MMIO; exact transfer is intentionally not claimed |
| Repeated reload | Three idle `rmmod`/`insmod` cycles and factorial after each | `reload_test.txt` | Verified only under idle conditions; no in-flight unbind test |

## Removal and unload assessment

Open files increment both the device kref and the module reference through
`.owner = THIS_MODULE`. Therefore `rmmod` while a file is open, including a
blocked reader or active ioctl, is rejected by the module reference count. A
PCI sysfs unbind or hot removal is a separate path: `remove()` marks the
instance dead, deregisters the node, wakes readers/completions, and waits for
`operation_lock`; open-file memory remains valid through the kref, and post-dead
operations return `-ENODEV`/EOF. This source path has not been runtime-tested
while a read, factorial, or DMA operation is active. DMA has no cancel
register; if RUN remains set during remove, the driver clears bus mastering and
retains the coherent allocation. This quarantine path was not runtime-injected.

The verified repeated load/unload claim is limited to three idle module reload
cycles. No claim of safe removal from a wedged or actively unbound device is
supported by current evidence.

## QEMU source references

- [QEMU v6.2.0 EDU specification](https://gitlab.com/qemu-project/qemu/-/raw/v6.2.0/docs/specs/edu.txt)
- [QEMU v6.2.0 EDU implementation](https://gitlab.com/qemu-project/qemu/-/raw/v6.2.0/hw/misc/edu.c)

## Final disposition after fixes and fresh verification

The initial audit identified 2 HIGH, 4 MEDIUM, and 3 LOW findings. Both HIGH
findings, three MEDIUM findings, and all LOW findings were fixed. One MEDIUM
coverage limitation remains: active PCI unbind/removal was not runtime-tested.
There are also three INFO scope caveats below.

| ID | Severity | Final disposition and evidence |
|---|---|---|
| HIGH-1 | HIGH | **Fixed.** Factorial and DMA error cleanup drain or check idle before clearing the active operation, synchronize the IRQ, and reject reuse if the engine remains faulted. DMA signal-interruption cleanup and subsequent data integrity pass in `evidence/dma_test.txt`; factorial cancellation remains source-reviewed. |
| HIGH-2 | HIGH | **Fixed.** Remove clears bus mastering before IRQ/DMA resource teardown. If DMA RUN will not clear, it disables INTx and intentionally retains the coherent buffer. The exceptional stuck-engine path was not runtime-injected. |
| MEDIUM-1 | MEDIUM | **Fixed.** `edu_read()` snapshots under the ring lock, copies outside it, and consumes records only after a successful user copy. `ring-test` confirms EFAULT preserves the event in `evidence/ring_test.txt`. |
| MEDIUM-2 | MEDIUM | **Fixed.** `docs/architecture.md` and `docs/resume_bullets.md` now state QEMU guest verification and 4095-byte largest tested transfer. |
| MEDIUM-3 | MEDIUM | **Fixed.** `ring-test` calls the kernel factorial ioctl with input 13 and checks `ERANGE`; see `evidence/ring_test.txt`. |
| MEDIUM-4 | MEDIUM | **Open validation limitation.** Three idle reload cycles pass. Active PCI sysfs unbind during read/factorial/DMA was not tested; no broader unload-under-load claim is supported. |
| LOW-1 | LOW | **Fixed.** `parse_u32()` rejects a leading minus sign; `test_mmio.sh` checks `-0` and `-1`. |
| LOW-2 | LOW | **Fixed.** `collect_evidence.sh` captures `dmesg` with failure propagation and writes an explicit health PASS/FAIL result. Fresh snapshot: `evidence/dmesg_snapshot.txt`. |
| LOW-3 | LOW | **Fixed.** `vm_setup.sh` reuses unpacked project-local host tools and preserves its package cache. |

### INFO scope caveats

#### INFO-1 — INTx fallback is not runtime-tested

- **File/function:** `driver/edu_main.c`, IRQ vector setup.
- **Problem:** MSI succeeded in the guest, so the legacy INTx fallback did not
  execute in the fresh run.
- **Why it matters:** Source review proves the fallback is present, not that a
  legacy interrupt is delivered and acknowledged on a runtime platform.
- **Evidence:** `evidence/module_load.txt` and `evidence/device_state_loaded.txt`
  show MSI IRQ 34; the full suite uses that same guest configuration.
- **Recommended scope:** Say MSI was runtime-tested and INTx fallback was
  source-reviewed only.

#### INFO-2 — The target is virtual PCI hardware

- **File/function:** `README.md`, `docs/resume_bullets.md`, project scope.
- **Problem:** QEMU EDU under QEMU/KVM does not establish physical-board or
  electrical PCIe bring-up experience.
- **Why it matters:** Those environments exercise different firmware, link,
  reset, and platform behavior.
- **Evidence:** Guest `lspci` identifies the emulated `1234:11e8` QEMU EDU
  function; `README.md` already limits the project to virtual hardware.
- **Recommended scope:** Describe this as a QEMU EDU virtual PCI driver.

#### INFO-3 — IOMMU, performance, and production behavior are not demonstrated

- **File/function:** DMA implementation and project claims.
- **Problem:** No IOMMU configuration matrix, throughput benchmark, physical
  hardware run, or production qualification was performed.
- **Why it matters:** The successful coherent DMA checks show correctness for
  this guest setup, not performance or portability across DMA domains.
- **Evidence:** The fresh report contains byte-for-byte functional tests only;
  there are no benchmark or IOMMU setup results.
- **Recommended scope:** Explain DMA API addresses and coherent memory, but do
  not claim IOMMU validation, performance gains, or production readiness.

### Final severity counts

| Severity | Open findings |
|---|---:|
| BLOCKER | 0 |
| HIGH | 0 |
| MEDIUM | 1 |
| LOW | 0 |
| INFO | 3 |

**Verdict: READY WITH MINOR CAVEATS.** The remaining MEDIUM item is a validation
boundary, not a demonstrated runtime failure. Keep the resume and interview
claims within the QEMU guest, MSI, 4095-byte transfer, and idle-reload evidence
recorded above.
