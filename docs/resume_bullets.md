# Resume bullets

## 中文

- 為 QEMU EDU PCI 裝置實作 Linux 核心字元驅動程式，涵蓋 PCI probe/remove、BAR0 MMIO、MSI 中斷完成通知及 `/dev/edupci` ioctl/read/poll 介面；在 Ubuntu 22.04.5 QEMU guest 中完成實機制式驗證。
- 實作有界事件環與 coherent DMA 雙向傳輸，驗證 FIFO、阻塞讀取、wraparound、overflow 計數，以及 1/257/4095-byte 傳輸；針對 QEMU 6.2 的 4096-byte 邊界錯誤加入安全拒絕，並以完整整合測試驗證卸載重載。

## English

- Implemented a Linux PCI character driver for QEMU EDU, including probe/remove, BAR0 MMIO, MSI completion interrupts, and `/dev/edupci` ioctl/read/poll interfaces; verified it in an Ubuntu 22.04.5 QEMU guest.
- Added a bounded event ring and bidirectional coherent DMA; verified FIFO, blocking reads, wraparound, overflow accounting, and 1/257/4095-byte transfers. Guarded QEMU 6.2's failing 4096-byte boundary and validated module reloads with the integration suite.
