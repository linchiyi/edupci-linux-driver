# EduPCI 學習指南（繁體中文）

本指南以 repository 的實作為例。讀 code 時先看 include/edu_ioctl.h 的 UAPI，再看 driver/edu_main.c，最後看 user/edu_cli.c 和 tests/。所有 MMIO/IRQ/DMA runtime 操作只在 QEMU guest 執行。

| 主題 | 它是什麼、為什麼存在 | 本專案用在哪裡 | 對應檔案／函式 | 面試官可能怎麼問 |
|---|---|---|---|---|
| 1. User mode / Kernel mode | User mode 權限受限；Kernel mode 可管理硬體與記憶體。隔離能避免一般程式直接破壞系統。 | CLI 透過裝置介面請 kernel driver 操作 PCI 裝置。 | user/edu_cli.c；driver/edu_main.c | 為什麼 userspace 不能直接寫 BAR？ |
| 2. System call | 受控地從 user mode 進入 kernel mode 的呼叫邊界。 | open/read/poll/ioctl 進入 VFS 與 driver。 | edu_open、edu_read、edu_poll、edu_ioctl | ioctl 的資料怎麼跨越權限邊界？ |
| 3. Device driver | 將 OS 通用介面轉成特定裝置操作的程式。 | 把 char-device 請求轉為 QEMU EDU 的 MMIO、IRQ、DMA 操作。 | driver/edu_main.c | driver 在 OS 與裝置之間做什麼？ |
| 4. Kernel module | 可載入/卸載的 kernel code，不需將 driver 編進 kernel image。 | insmod 載入 PCI driver；rmmod 執行 remove/exit。 | module_pci_driver、scripts/load_driver.sh | module unload 時誰呼叫 remove？ |
| 5. PCI model | PCI bus 透過 config space 枚舉裝置、分配資源並匹配 driver。 | QEMU guest 中用 vendor/device ID match EDU。 | edu_pci_ids、edu_probe | PCI enumeration 與 probe 的關係？ |
| 6. Vendor ID / Device ID | PCI config space 中辨識廠商與裝置型號的 16-bit 欄位。 | QEMU EDU 是 1234:11e8。 | PCI_DEVICE(0x1234, 0x11e8) | driver 如何避免綁到錯的裝置？ |
| 7. BAR | Base Address Register 描述裝置所需的 I/O 或 memory resource。 | BAR0 是 1 MiB MMIO 區域。 | edu_probe、pci_resource_len | 為什麼要 request_regions？ |
| 8. MMIO | 用 memory-addressed load/store 存取裝置寄存器；讀寫可能有副作用。 | inversion、factorial、IRQ、DMA registers。 | EDU_REG_*；edu_do_factorial | MMIO 和普通 RAM 的差異？ |
| 9. readl() / writel() | Linux 提供的有序 MMIO 讀寫 API，處理架構差異與型別。 | 所有 BAR0 registers 都使用 readl/writel。 | edu_irq_handler、edu_do_dma | 為什麼不能把 BAR cast 成指標再解參考？ |
| 10. Character device | 將裝置呈現為 file descriptor，讓程式使用 read/ioctl/poll 等標準操作。 | /dev/edupci 是 misc character device。 | edu_miscdevice、edu_fops | miscdevice 對這個小專案有什麼好處？ |
| 11. /dev | userspace 裝置節點，連到 kernel device interface。 | CLI 開啟 /dev/edupci。節點 mode 0600 限定 root 存取。 | edu_miscdevice.name/mode | 節點和 driver 本身是什麼關係？ |
| 12. ioctl | 適合傳遞有型別、非串流命令/結構的 driver ABI。 | info、invert、factorial、DMA、stats。 | edu_ioctl.h；edu_ioctl | 什麼時候用 ioctl、什麼時候用 read？ |
| 13. Blocking I/O | 沒有資料時讓 task 睡眠，事件到達再喚醒，避免 busy-wait。 | 空 ring 的 read 等待 read_wait。 | edu_read | blocking read 如何被喚醒？ |
| 14. Polling | 主動重複檢查狀態；簡單但可能浪費 CPU 或延遲不穩。 | 只在 timeout/error recovery 等 idle check 有界使用；正常完成不以 polling 代替 IRQ。 | edu_wait_factorial_idle、edu_wait_dma_idle | polling 和 interrupt 的取捨？ |
| 15. Interrupt | 裝置以非同步事件通知 CPU，CPU 不必持續檢查狀態。 | factorial cause 0x01、DMA cause 0x100。 | edu_setup_irq、edu_irq_handler | 如何證明請求走過 IRQ？ |
| 16. ISR / interrupt handler | IRQ context 的短函式，處理原因並喚醒可睡眠的 process context。 | ack cause、push event、complete waiter。 | edu_irq_handler | handler 可以做 copy_to_user 嗎？ |
| 17. Process context | 由 task 執行，可睡眠並取得 mutex。 | ioctl/read/remove 等路徑。 | edu_do_factorial、edu_do_dma、edu_read | 哪些 driver 操作可以睡？ |
| 18. Interrupt context | IRQ handler 執行環境，不可睡眠，必須盡快返回。 | event push 與 completion signal。 | edu_irq_handler、edu_push_event | handler 若呼叫 mutex_lock 會怎樣？ |
| 19. Sleeping / non-sleeping context | 睡眠操作可能 schedule；spinlock/MMIO 等需在不可睡環境安全使用。 | process 用 mutex/completion；handler 用 spinlock/atomic。 | concurrency_model.md | 為何 spinlock 區段不能 copy_to_user？ |
| 20. Wait queue | 讓 task 在條件不成立時睡眠，條件改變時喚醒。 | event ring read/poll readiness。 | read_wait、edu_read、edu_poll | 怎麼避免喚醒前後的 lost wakeup？ |
| 21. Completion | 一次性或計數式完成同步原語，適合等待某個工作完成。 | factorial_done、dma_done 連結 ioctl 與 IRQ。 | edu_probe、edu_do_factorial、edu_irq_handler | 為何這裡選 completion？ |
| 22. Race condition | 執行交錯讓共享狀態結果不確定。 | IRQ producer 與 read consumer 同時改 ring。 | ring_head/tail/count | 沒有 ring lock 會出現什麼錯誤？ |
| 23. Mutex | 可睡眠的互斥鎖，適用 process context 的較長工作。 | operation_lock 保護單一硬體操作；read_lock 序列化 readers。 | edu_device、edu_do_dma | mutex 可否在 ISR 使用？ |
| 24. Spinlock | 短時間忙等互斥，適用不可睡 context。 | ring_lock 保護 IRQ/process 共用 ring。 | edu_push_event、edu_read | 為什麼讀取 ring 時要 irqsave？ |
| 25. Atomic operation | 對單一計數/值做不可分割更新，不足以保護多欄位不變條件。 | IRQ/statistics counters。 | atomic64_t 欄位、edu_irq_handler | 為什麼 atomic 不能取代 ring lock？ |
| 26. Producer-consumer | producer 產生資料，consumer 以自己的節奏取用，中間需有佇列和同步。 | IRQ 是 producer、read 是 consumer。 | edu_push_event、edu_read | producer 快於 consumer 時怎麼處理？ |
| 27. Ring buffer | 固定容量的循環佇列；head/tail 繞回以重用儲存空間。 | 64 筆 completion events，滿時 drop-new 並計數。 | ring、edu_push_event、edu_read | 如何判斷滿/空？wraparound 怎麼驗證？ |
| 28. DMA | 裝置直接讀寫 host memory，避免 CPU 逐 byte 搬運。 | QEMU EDU DMA engine 對內部 4096-byte buffer；本機 QEMU 6.2 的實際測試上限是 4095 bytes。 | edu_do_dma、command_dma_test | DMA completion 如何通知 driver？ |
| 29. CPU virtual address | process/kernel 程式看到的虛擬位址，由 MMU 翻譯。 | CLI user buffer、kernel coherent buffer 各有 CPU 可用位址。 | edu_dma_request.user_buffer、dma_cpu | 為什麼 CPU pointer 不等於 DMA address？ |
| 30. Physical address | 系統記憶體的實體位址；CPU virtual address 需經 page table 翻譯。 | driver 不直接推測 coherent allocation 的 physical address。 | edu_do_dma | 一個 pointer 如何轉成 physical address？ |
| 31. DMA address | 裝置 DMA domain 使用的位址，可能由 IOMMU 轉譯，不應由 CPU pointer cast。 | dma_alloc_coherent 回傳 dma_address。 | edu_probe、edu_do_dma | DMA API 回傳的兩個位址各給誰用？ |
| 32. Coherent DMA | CPU/device 對同一配置保持一致可見的 DMA memory；仍需 ordering barrier。 | 4096-byte H2D/D2H transfer buffer。 | dma_alloc_coherent、dma_wmb、dma_rmb | coherent 是否代表完全不需 barrier？ |
| 33. Cache coherence | 多個觀察者看到記憶體更新的可見性問題；DMA API 隔離平台差異。 | coherent buffer 配合 dma_wmb/rmb 控制資料與命令順序。 | edu_do_dma | non-coherent 平台會有什麼額外工作？ |
| 34. Cleanup / error unwinding | 每個成功取得的資源都要在後續失敗或 remove 時釋放，避免 leak/use-after-free。 | PCI enable、regions、BAR、IRQ、DMA、misc node 按生命週期配對。 | edu_probe、edu_free_resources、edu_remove | probe 第五步失敗時前四步如何回收？ |

## 建議學習順序

先能解釋 PCI match → probe → BAR → MMIO；再學 ioctl 與 process/IRQ context；接著用 ring_lock、completion 和 wait queue 解釋同步；最後說明 DMA address、coherent buffer 和 cleanup。每個概念都回到表中的檔案和函式，不要只背名詞。
