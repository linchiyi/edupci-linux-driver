# 1–2 分鐘 Demo 講稿

1. **PCI 裝置**：「這個專案在 Ubuntu QEMU guest 裡使用 QEMU EDU 虛擬 PCI 裝置，PCI ID 是 `1234:11e8`。它不是實體板子的 driver。」
2. **Driver 載入**：「載入模組後，probe 會啟用 PCI function、要求並映射 BAR0、設定 DMA mask、配置 IRQ，最後建立 `/dev/edupci`。」
3. **MMIO**：「我透過 `readl()` / `writel()` 測試裝置的反轉暫存器，四組輸入都由程式比較預期值。」
4. **Interrupt**：「factorial ioctl 會啟動 EDU 的計算並睡在 completion。裝置完成後送 IRQ，handler 清除原因、記錄事件並喚醒等待中的 process。」
5. **Ring buffer**：「IRQ handler 是 producer，`read()` 是 consumer。測試會檢查 FIFO、索引繞回，以及滿載時 drop-new 和 overflow 計數。」
6. **DMA**：「最後用 coherent DMA buffer 測試 host-to-device 和 device-to-host；資料以多種長度逐 byte 比對。」

執行 `make demo`。畫面中的每個 PASS 都在 guest 執行實際命令後才輸出。錄影時可補充：這是 QEMU EDU 虛擬硬體的學習與驗證，不代表實體 PCIe bring-up 經驗。

