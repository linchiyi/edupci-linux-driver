#ifndef EDUPCI_IOCTL_H
#define EDUPCI_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define EDU_IOCTL_MAGIC       'E'
#define EDU_EVENT_CAPACITY    64U
#define EDU_DMA_BUFFER_SIZE   4096U
/* Avoid QEMU EDU 6.2's exact-end range-check bug; see qemu_edu_registers.md. */
#define EDU_DMA_MAX_LENGTH    (EDU_DMA_BUFFER_SIZE - 1U)

enum edu_event_type {
	EDU_EVENT_FACTORIAL = 1,
	EDU_EVENT_DMA = 2,
};

enum edu_dma_direction {
	EDU_DMA_HOST_TO_DEVICE = 0,
	EDU_DMA_DEVICE_TO_HOST = 1,
};

struct edu_event {
	__aligned_u64 sequence;
	__u32 type;
	__u32 value;
};

struct edu_invert {
	__u32 input;
	__u32 result;
};

struct edu_factorial {
	__u32 input;
	__u32 result;
};

struct edu_dma_request {
	__u32 direction;
	__u32 length;
	__aligned_u64 user_buffer;
};

struct edu_info {
	__u32 vendor_id;
	__u32 device_id;
	__u32 device_version;
	__u32 bar0_size;
	__u32 irq_mode;
	__u32 event_capacity;
};

struct edu_stats {
	__aligned_u64 irq_count;
	__aligned_u64 factorial_count;
	__aligned_u64 dma_count;
	__aligned_u64 event_overflows;
	__aligned_u64 last_sequence;
};

#define EDU_IRQ_MODE_INTX  1U
#define EDU_IRQ_MODE_MSI   2U

#define EDU_IOC_GET_INFO    _IOR(EDU_IOCTL_MAGIC, 0x00, struct edu_info)
#define EDU_IOC_INVERT      _IOWR(EDU_IOCTL_MAGIC, 0x01, struct edu_invert)
#define EDU_IOC_FACTORIAL   _IOWR(EDU_IOCTL_MAGIC, 0x02, struct edu_factorial)
#define EDU_IOC_DMA         _IOW(EDU_IOCTL_MAGIC, 0x03, struct edu_dma_request)
#define EDU_IOC_GET_STATS   _IOR(EDU_IOCTL_MAGIC, 0x04, struct edu_stats)

#endif
