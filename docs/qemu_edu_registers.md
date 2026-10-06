# QEMU EDU 使用的暫存器

依據本機 QEMU 6.2 套件附帶的 /usr/share/doc/qemu-system-data/specs/edu.txt，並核對 QEMU 專案 v6.2.0 的 [EDU 規格](https://gitlab.com/qemu-project/qemu/-/raw/v6.2.0/docs/specs/edu.txt) 與 [裝置實作](https://gitlab.com/qemu-project/qemu/-/raw/v6.2.0/hw/misc/edu.c)。本專案只使用以下子集。BAR0 是 1 MiB memory BAR，以下所有 MMIO 存取均由 driver 使用 readl() / writel()，不當作普通 RAM。

| Offset | 大小 | 讀寫 | 語意與本專案用途 | Interrupt |
|---:|---:|---|---|---|
| 0x00 | 4 | RO | 裝置識別值；本機 QEMU 6.2 回報 0x010000ed。probe 驗證低 8 bits 為 0xed。 | 無 |
| 0x04 | 4 | RW | 寫入 x 後可讀回 ~x，作為 liveness/MMIO 驗證。 | 無 |
| 0x08 | 4 | RW | 寫入 n 啟動 factorial；完成後讀回結果。專案限制 0..12，避免 32-bit overflow。 | 若 0x20 的 0x80 已啟用，完成時 raise 0x01。 |
| 0x20 | 4 | RW | bit 0x01 是硬體計算中狀態；寫入 bit 0x80 啟用 factorial 完成 IRQ，寫 0 清除此 enable。 | 使 factorial 完成時 raise IRQ。 |
| 0x24 | 4 | RO | Interrupt cause/status。 | 0x01 factorial，0x100 DMA。 |
| 0x64 | 4 | WO | 寫入要清除的 cause bits；ISR 對讀出的原因值寫回此處。 | 清除 pending IRQ cause。 |
| 0x80 | 4 | RW | DMA source address。EDU 預設 DMA mask 是 28-bit。 | 無 |
| 0x88 | 4 | RW | DMA destination address。 | 無 |
| 0x90 | 4 | RW | DMA byte count。 | 無 |
| 0x98 | 4 | RW | 寫入 command 啟動 DMA：bit 0x01 RUN、bit 0x02 表示 EDU-to-host、bit 0x04 要求完成 IRQ 0x100。 | 完成時 raise DMA IRQ。 |

裝置內部 DMA buffer 位於 BAR0 offset 0x40000，長度 4096 bytes。Host-to-device 方向把 coherent host buffer 指定為 source、0x40000 指定為 destination；device-to-host 方向相反。Driver 設 DMA_BIT_MASK(28) 並以 dma_alloc_coherent() 取得 DMA address，絕不將 CPU virtual address 直接當成 DMA address。

QEMU EDU 規格允許 0x80 以後使用 4 或 8-byte 存取；本專案使用 4-byte DMA addresses，並檢查 DMA address 可放入 32 bits。

### 本機 QEMU 6.2 的 DMA 邊界限制

規格描述的內部 buffer 為 4096 bytes，但 QEMU v6.2.0 的 hw/misc/edu.c 中，within() 使用 end-exclusive 範圍，而 edu_check_range() 也要求傳輸結尾仍位於範圍內。剛好 4096 bytes 的傳輸會把 end1 放在範圍上界，QEMU 會以 `hardware error: EDU: DMA range ... out of bounds` 結束整個 emulator。這是裝置模擬實作的 exact-end off-by-one 行為，不是 driver 可以安全忽略的錯誤。

因此本專案限制單次 transfer 為 1..4095 bytes，並以 ioctl 在觸及裝置前拒絕 4096。測試使用 1、257、4095 bytes，涵蓋小、中與接近最大值的實際傳輸。參考 QEMU v6.2.0 的 [range check](https://gitlab.com/qemu-project/qemu/-/raw/v6.2.0/hw/misc/edu.c)；`edu_check_range()` 位於 within() 後方。

Factorial operation 的次序是：清除舊 IRQ cause、清除 IRQ enable、重設 completion、啟用 bit 0x80、寫入 0x08。DMA 次序是：設定 source/destination/count、寫入含 RUN 與 IRQ bits 的 command。兩者都以 interrupt completion 作為一般完成路徑；等待有 3 秒上限。
