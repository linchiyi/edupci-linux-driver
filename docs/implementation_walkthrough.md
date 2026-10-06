# 實作導讀

本文對應本 repository 的實際檔案與函式。核心實作集中在 driver/edu_main.c，UAPI 在 include/edu_ioctl.h，CLI 在 user/edu_cli.c。以下的「context」描述函式執行環境。

## 從 VM 到 module

- scripts/vm_setup.sh 下載 Ubuntu Jammy cloud image、建立 qcow2 overlay、產生專用 ed25519 SSH key 和 cloud-init seed。
- scripts/vm_start.sh 使用 q35、KVM、2 vCPU、2 GiB RAM、-device edu，以及只綁 localhost 的 SSH port forwarding。
- scripts/guest_provision.sh 在 guest 安裝執行 kernel build 所需的 headers 和工具，並保存 uname、release、lspci 和編譯器版本。
- scripts/sync_to_guest.sh 傳送 source，scripts/build_guest.sh 在 guest 內 clean/build kernel module 和 CLI。這些命令不會對 WSL kernel 執行 insmod。
- driver/Makefile 將 edu_main.o 組成 edupci.ko；user/Makefile 以 -Wall -Wextra -Wpedantic 建立 edu_cli。

## edu_probe()

- **輸入：** PCI core 傳入已 match 1234:11e8 的 pci_dev。
- **輸出：** 成功回 0 並完成 binding；失敗回負 errno，釋放此前取得的資源。
- **重要區域：** 先以 singleton mutex 保留唯一的 misc-device 實例，配置 edu_device 並初始化 mutex、spinlock、wait queue、completion 和統計值。
- **資源取得次序：** pci_enable_device() → 確認 BAR0 是 memory resource → pci_request_regions() → pci_iomap() → readl() 驗證識別值 → dma_set_mask_and_coherent(28 bits) → dma_alloc_coherent() → pci_set_master() → 分配 MSI 或退回 INTx → request_irq() → misc_register()。
- **執行 context：** PCI probe 的 process context，可睡眠。
- **同步：** singleton mutex 保護單一 /dev/edupci；初始 kref 屬於 PCI binding。
- **錯誤情況：** 每個成功取得的資源由 edu_free_resources() 逆序釋放。未建立 device node 的失敗不會留下半成品介面。

## edu_setup_irq() 與 edu_irq_handler()

- **edu_setup_irq 輸入/輸出：** 取得已初始化的 edu_device；先嘗試一個 MSI vector，失敗再試 legacy INTx，呼叫 request_irq() 後記錄模式與 IRQ 編號。
- **handler 輸入：** Linux IRQ number 與 edu_device 指標；**輸出：** IRQ_NONE（沒有本裝置 cause）或 IRQ_HANDLED。
- **逐步：** (1) readl(0x24) 取 cause；零代表共享 INTx 不是本裝置的 IRQ。(2) writel(cause, 0x64) acknowledge。(3) atomic64 增加 IRQ 計數。(4) 依 active_operation 判斷 factorial 或 DMA。(5) 讀回 factorial result 或 DMA byte count。(6) push event。(7) complete 對應等待者。
- **context：** hard IRQ；不可 sleep。handler 不取 operation_lock、不 copy_to_user、不做大筆 DMA 資料處理。
- **失敗/競態：** cause 先 acknowledge；即使沒有等待者，也能避免裝置持續 assert 同一個 IRQ。active_operation 避免把未預期原因當成本次完成。

## edu_do_factorial()

- **輸入：** UAPI struct 的 input 0..12；**輸出：**同一 struct 的 result。
- **重要區域：** request；waited 保存 completion 等待結果；ret 保存 Linux errno。
- **逐步：** 驗證範圍；拿 operation_lock；確認 device 未移除且硬體 idle；清 IRQ enable 並 acknowledge 舊 cause；reinit_completion；設定 active_operation；寫 0x80 到 status、寫 n 到 factorial register；用最長 3 秒的 wait_for_completion_interruptible_timeout() 讓 task 睡眠；成功後讀回結果；清 active、停用 IRQ enable、ack 殘留 cause。
- **context：** ioctl process context，可以睡眠。
- **同步：** operation_lock 只允許一個 factorial/DMA request 操作共用硬體暫存器；completion 對應 IRQ handler。
- **錯誤：** >12 回 -ERANGE；訊號中斷回 -ERESTARTSYS；3 秒沒完成回 -ETIMEDOUT；device 被拔除回 -ENODEV。失敗路徑仍停用 IRQ，並有界等待 computing bit 清除。

## edu_push_event() 與 edu_read()

- **push 輸入：** event type、value；**輸出：**插入時遞增 sequence，滿載時遞增 overflow 並丟棄新事件。
- **push locals：** flags 用於 spin_lock_irqsave；queued 決定解鎖後是否 wake up。
- **read 輸入：** userspace buffer 和 bytes count；**輸出：**讀取的 event bytes，空且已移除時回 EOF。
- **read 流程：** 檢查 count 是 event 大小的倍數；read_lock 序列化 consumer；空時對 nonblocking fd 回 -EAGAIN，否則最多等待 30 秒的 wait_event_interruptible_timeout；spin_lock_irqsave 內將至多 32 筆搬到 kernel stack、更新 tail/count；解鎖後 copy_to_user。ring-test 會 fork 一個 blocking reader，等另一個 request 產生事件後確認它醒來並讀到 event。
- **context：** push 在 IRQ；read 在 process context。
- **同步：** ring_lock 保護資料與索引。read_lock 只序列化多個 userspace reader；copy_to_user 不在 spinlock 內。
- **限制：** 若 copy_to_user 發生 fault，已從 ring 移出的該批事件不會放回。正常 CLI 傳入有效 buffer。

## edu_do_dma()

- **輸入：** direction、length、64-bit userspace buffer pointer；**輸出：**成功代表實際 DMA IRQ 完成；D2H 成功時將資料複製回 userspace。
- **逐步：** 檢查方向、長度 1..4095 和 DMA address 上半部；拿 operation_lock；H2D 先 copy_from_user 到 coherent buffer；ack 舊 cause、重設 completion、標 active；H2D 執行 dma_wmb 並寫 RAM address → EDU 0x40000，D2H 寫 EDU 0x40000 → RAM address；寫 count 與 RUN|IRQ command；等待 dma_done 最長 3 秒；確認 RUN 清除；D2H 執行 dma_rmb 並 copy_to_user。
- **context：** ioctl process context可睡眠；硬體和 IRQ 在等待期間工作。
- **同步：** operation_lock 避免共用 coherent buffer 或 DMA 暫存器被重疊使用。CPU virtual address 和 dma_addr_t 分開保存。
- **錯誤：** 超長傳輸回 -EINVAL，使用者 copy 錯誤回 -EFAULT，等待 timeout 回 -ETIMEDOUT。錯誤後有界等待 RUN 清除，避免下一個 request 覆蓋仍使用中的 buffer。

## edu_remove() 與資源釋放

- **輸入：** PCI core 傳入移除中的 pci_dev；**輸出：**無。
- **逐步：**標 dead、從 misc singleton 移除並 deregister；wake read waiters、complete 進行中的等待；拿 operation_lock 等目前 ioctl 離開；停用 factorial IRQ、ack pending cause；free_irq 等待 handler 退出；free MSI vector、清 bus master、free coherent buffer、iounmap、release regions、disable PCI function；清 PCI drvdata，釋放 binding kref。
- **同步：** open file 另持有 kref，因此 file 記憶體仍有效；dead 後 file operation 不再碰 PCI resource。free_irq 前 BAR 仍映射。
- **錯誤情況：** EDU 規格沒有 DMA cancel register。正常 remove 等 DMA RUN 清除且有 3 秒 timeout，並留下 kernel warning 供 evidence 檢查。

## CLI 與 ABI

- include/edu_ioctl.h 使用 __u32 / __aligned_u64；pointer 欄位固定 64-bit，32-bit compat ioctl 也使用固定結構。
- info 讀裝置 ID、版本、BAR size 和 IRQ mode；invert 比對獨立算出的 bitwise complement；factorial 檢查結果及 IRQ counter；ring-test 驗證空、poll、單筆、FIFO、跨索引繞回、滿載 drop-new 與 overflow；dma-test 驗證 1、257、4095 bytes 的雙向內容，並確認 4096 bytes 在啟動硬體前被拒絕。
- 每個 ioctl 都是明確的 kernel/user copy；userspace 不可直接映射或 dereference BAR0。
