// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../include/edu_ioctl.h"

#define EDU_VENDOR_ID              0x1234
#define EDU_DEVICE_ID              0x11e8

#define EDU_REG_IDENTIFICATION     0x00
#define EDU_REG_INVERT             0x04
#define EDU_REG_FACTORIAL          0x08
#define EDU_REG_STATUS             0x20
#define EDU_REG_IRQ_STATUS         0x24
#define EDU_REG_IRQ_ACK            0x64
#define EDU_REG_DMA_SOURCE         0x80
#define EDU_REG_DMA_DESTINATION    0x88
#define EDU_REG_DMA_COUNT          0x90
#define EDU_REG_DMA_COMMAND        0x98

#define EDU_STATUS_COMPUTING       0x01
#define EDU_STATUS_IRQ_FACTORIAL   0x80
#define EDU_IRQ_FACTORIAL          0x01
#define EDU_IRQ_DMA                0x100
#define EDU_DMA_DEVICE_BUFFER      0x40000
#define EDU_DMA_RUN                0x01
#define EDU_DMA_TO_HOST            0x02
#define EDU_DMA_RAISE_IRQ          0x04

#define EDU_ACTIVE_NONE            0
#define EDU_ACTIVE_FACTORIAL       1
#define EDU_ACTIVE_DMA             2
#define EDU_OPERATION_TIMEOUT_MS   3000
#define EDU_READ_TIMEOUT_MS        30000
#define EDU_READ_BATCH             32U

struct edu_device {
	struct kref refcount;
	struct pci_dev *pdev;
	void __iomem *bar0;
	resource_size_t bar0_len;
	int irq;
	u32 irq_mode;
	bool irq_vectors_allocated;
	bool irq_requested;
	bool regions_requested;
	bool device_enabled;
	bool bus_master_enabled;
	bool dead;
	bool engine_faulted;
	int active_operation;

	struct mutex operation_lock;
	struct mutex read_lock;
	spinlock_t ring_lock;
	wait_queue_head_t read_wait;
	struct completion factorial_done;
	struct completion dma_done;

	struct edu_event ring[EDU_EVENT_CAPACITY];
	u32 ring_head;
	u32 ring_tail;
	u32 ring_count;
	u64 last_sequence;
	u64 event_overflows;

	void *dma_cpu;
	dma_addr_t dma_address;

	atomic64_t irq_count;
	atomic64_t factorial_count;
	atomic64_t dma_count;
};

static DEFINE_MUTEX(edu_singleton_lock);
static struct edu_device *edu_singleton;
static bool edu_claimed;
static struct miscdevice edu_miscdevice;

static void edu_device_release(struct kref *ref)
{
	struct edu_device *dev = container_of(ref, struct edu_device, refcount);

	kfree(dev);
}

static bool edu_events_ready(struct edu_device *dev)
{
	unsigned long flags;
	bool ready;

	spin_lock_irqsave(&dev->ring_lock, flags);
	ready = dev->ring_count != 0;
	spin_unlock_irqrestore(&dev->ring_lock, flags);
	return ready;
}

static void edu_push_event(struct edu_device *dev, u32 type, u32 value)
{
	unsigned long flags;
	bool queued = false;
	struct edu_event event;

	spin_lock_irqsave(&dev->ring_lock, flags);
	event.sequence = ++dev->last_sequence;
	event.type = type;
	event.value = value;
	if (dev->ring_count == EDU_EVENT_CAPACITY) {
		dev->event_overflows++;
	} else {
		dev->ring[dev->ring_head] = event;
		dev->ring_head = (dev->ring_head + 1) % EDU_EVENT_CAPACITY;
		dev->ring_count++;
		queued = true;
	}
	spin_unlock_irqrestore(&dev->ring_lock, flags);

	if (queued)
		wake_up_interruptible(&dev->read_wait);
}

/*
 * Acknowledge the EDU interrupt cause in hard IRQ context. The handler only
 * publishes a small event and completes the matching process-context wait.
 */
static irqreturn_t edu_irq_handler(int irq, void *opaque)
{
	struct edu_device *dev = opaque;
	u32 causes;
	int active;

	causes = readl(dev->bar0 + EDU_REG_IRQ_STATUS);
	if (!causes)
		return IRQ_NONE;

	writel(causes, dev->bar0 + EDU_REG_IRQ_ACK);
	atomic64_inc(&dev->irq_count);
	active = READ_ONCE(dev->active_operation);

	if ((causes & EDU_IRQ_FACTORIAL) &&
	    active == EDU_ACTIVE_FACTORIAL) {
		u32 value = readl(dev->bar0 + EDU_REG_FACTORIAL);

		atomic64_inc(&dev->factorial_count);
		edu_push_event(dev, EDU_EVENT_FACTORIAL, value);
		complete(&dev->factorial_done);
	}
	if ((causes & EDU_IRQ_DMA) && active == EDU_ACTIVE_DMA) {
		u32 length = readl(dev->bar0 + EDU_REG_DMA_COUNT);

		atomic64_inc(&dev->dma_count);
		edu_push_event(dev, EDU_EVENT_DMA, length);
		complete(&dev->dma_done);
	}

	return IRQ_HANDLED;
}

static int edu_wait_factorial_idle(struct edu_device *dev,
				   unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (!(readl(dev->bar0 + EDU_REG_STATUS) & EDU_STATUS_COMPUTING))
			return 0;
		usleep_range(1000, 2000);
	} while (time_before(jiffies, deadline));

	return -ETIMEDOUT;
}

static int edu_wait_dma_idle(struct edu_device *dev, unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (!(readl(dev->bar0 + EDU_REG_DMA_COMMAND) & EDU_DMA_RUN))
			return 0;
		usleep_range(1000, 2000);
	} while (time_before(jiffies, deadline));

	return -ETIMEDOUT;
}

static void edu_ack_pending_irqs(struct edu_device *dev)
{
	u32 causes = readl(dev->bar0 + EDU_REG_IRQ_STATUS);

	if (causes)
		writel(causes, dev->bar0 + EDU_REG_IRQ_ACK);
}

static long edu_do_factorial(struct edu_device *dev,
			     struct edu_factorial *request)
{
	long waited;
	long drain_waited;
	bool check_idle = false;
	int ret;

	if (request->input > 12)
		return -ERANGE;
	if (mutex_lock_interruptible(&dev->operation_lock))
		return -ERESTARTSYS;
	if (READ_ONCE(dev->dead)) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (dev->engine_faulted) {
		ret = -EIO;
		goto out_unlock;
	}

	ret = edu_wait_factorial_idle(dev, EDU_OPERATION_TIMEOUT_MS);
	if (ret) {
		dev->engine_faulted = true;
		goto out_unlock;
	}

	/* Drop a stale cause before arming this request's completion. */
	writel(0, dev->bar0 + EDU_REG_STATUS);
	edu_ack_pending_irqs(dev);
	reinit_completion(&dev->factorial_done);
	WRITE_ONCE(dev->active_operation, EDU_ACTIVE_FACTORIAL);
	writel(EDU_STATUS_IRQ_FACTORIAL, dev->bar0 + EDU_REG_STATUS);
	writel(request->input, dev->bar0 + EDU_REG_FACTORIAL);

	waited = wait_for_completion_interruptible_timeout(
		&dev->factorial_done, msecs_to_jiffies(EDU_OPERATION_TIMEOUT_MS));
	if (waited == 0)
		ret = -ETIMEDOUT;
	else if (waited < 0)
		ret = waited;
	else if (READ_ONCE(dev->dead))
		ret = -ENODEV;
	else {
		request->result = readl(dev->bar0 + EDU_REG_FACTORIAL);
		ret = 0;
	}

	/*
	 * Keep the operation active while draining a completion after a signal or
	 * timeout. Otherwise a late IRQ could be mistaken for the next request.
	 */
	if (waited <= 0 || READ_ONCE(dev->dead)) {
		if (waited <= 0 && !READ_ONCE(dev->dead)) {
			drain_waited = wait_for_completion_timeout(
				&dev->factorial_done,
				msecs_to_jiffies(EDU_OPERATION_TIMEOUT_MS));
			check_idle = !drain_waited;
		} else {
			check_idle = true;
		}
	}
	if (READ_ONCE(dev->dead))
		check_idle = true;
	if (check_idle && edu_wait_factorial_idle(dev,
						 EDU_OPERATION_TIMEOUT_MS)) {
		dev->engine_faulted = true;
		dev_err(&dev->pdev->dev,
			"factorial engine did not become idle; rejecting later operations\n");
	}
	writel(0, dev->bar0 + EDU_REG_STATUS);
	synchronize_irq(dev->irq);
	edu_ack_pending_irqs(dev);
	WRITE_ONCE(dev->active_operation, EDU_ACTIVE_NONE);
out_unlock:
	mutex_unlock(&dev->operation_lock);
	return ret;
}

static long edu_do_dma(struct edu_device *dev, struct edu_dma_request *request)
{
	void __user *user_buffer = u64_to_user_ptr(request->user_buffer);
	u32 command;
	long waited;
	int ret;

	if (!request->length || request->length > EDU_DMA_MAX_LENGTH)
		return -EINVAL;
	if (request->direction != EDU_DMA_HOST_TO_DEVICE &&
	    request->direction != EDU_DMA_DEVICE_TO_HOST)
		return -EINVAL;
	if (upper_32_bits(dev->dma_address))
		return -ERANGE;

	if (mutex_lock_interruptible(&dev->operation_lock))
		return -ERESTARTSYS;
	if (READ_ONCE(dev->dead)) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (dev->engine_faulted) {
		ret = -EIO;
		goto out_unlock;
	}
	ret = edu_wait_dma_idle(dev, EDU_OPERATION_TIMEOUT_MS);
	if (ret) {
		dev->engine_faulted = true;
		goto out_unlock;
	}

	if (request->direction == EDU_DMA_HOST_TO_DEVICE &&
	    copy_from_user(dev->dma_cpu, user_buffer, request->length)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	edu_ack_pending_irqs(dev);
	reinit_completion(&dev->dma_done);
	WRITE_ONCE(dev->active_operation, EDU_ACTIVE_DMA);
	if (request->direction == EDU_DMA_HOST_TO_DEVICE) {
		dma_wmb();
		writel(lower_32_bits(dev->dma_address),
		       dev->bar0 + EDU_REG_DMA_SOURCE);
		writel(EDU_DMA_DEVICE_BUFFER,
		       dev->bar0 + EDU_REG_DMA_DESTINATION);
		command = EDU_DMA_RUN | EDU_DMA_RAISE_IRQ;
	} else {
		writel(EDU_DMA_DEVICE_BUFFER,
		       dev->bar0 + EDU_REG_DMA_SOURCE);
		writel(lower_32_bits(dev->dma_address),
		       dev->bar0 + EDU_REG_DMA_DESTINATION);
		command = EDU_DMA_RUN | EDU_DMA_TO_HOST | EDU_DMA_RAISE_IRQ;
	}
	writel(request->length, dev->bar0 + EDU_REG_DMA_COUNT);
	writel(command, dev->bar0 + EDU_REG_DMA_COMMAND);

	waited = wait_for_completion_interruptible_timeout(
		&dev->dma_done, msecs_to_jiffies(EDU_OPERATION_TIMEOUT_MS));
	if (waited == 0)
		ret = -ETIMEDOUT;
	else if (waited < 0)
		ret = waited;
	else if (READ_ONCE(dev->dead))
		ret = -ENODEV;
	else if (readl(dev->bar0 + EDU_REG_DMA_COMMAND) & EDU_DMA_RUN)
		ret = -EIO;
	else
		ret = 0;

	if (ret) {
		if (waited <= 0 && !READ_ONCE(dev->dead))
			wait_for_completion_timeout(&dev->dma_done,
				msecs_to_jiffies(EDU_OPERATION_TIMEOUT_MS));
		if (edu_wait_dma_idle(dev, EDU_OPERATION_TIMEOUT_MS)) {
			dev->engine_faulted = true;
			dev_err(&dev->pdev->dev,
				"DMA engine did not become idle; rejecting later operations\n");
		}
	}
	synchronize_irq(dev->irq);
	edu_ack_pending_irqs(dev);
	WRITE_ONCE(dev->active_operation, EDU_ACTIVE_NONE);

	if (!ret && request->direction == EDU_DMA_DEVICE_TO_HOST) {
		dma_rmb();
		if (copy_to_user(user_buffer, dev->dma_cpu, request->length))
			ret = -EFAULT;
	}
out_unlock:
	mutex_unlock(&dev->operation_lock);
	return ret;
}

static int edu_open(struct inode *inode, struct file *file)
{
	struct edu_device *dev;

	if (file->private_data != &edu_miscdevice)
		return -ENODEV;

	mutex_lock(&edu_singleton_lock);
	dev = edu_singleton;
	if (!dev || READ_ONCE(dev->dead) ||
	    !kref_get_unless_zero(&dev->refcount)) {
		mutex_unlock(&edu_singleton_lock);
		return -ENODEV;
	}
	mutex_unlock(&edu_singleton_lock);
	file->private_data = dev;
	return 0;
}

static int edu_release(struct inode *inode, struct file *file)
{
	struct edu_device *dev = file->private_data;

	kref_put(&dev->refcount, edu_device_release);
	return 0;
}

static ssize_t edu_read(struct file *file, char __user *buffer,
			size_t count, loff_t *offset)
{
	struct edu_device *dev = file->private_data;
	struct edu_event events[EDU_READ_BATCH];
	size_t max_events;
	u32 copied;
	u32 i;
	unsigned long flags;
	ssize_t ret;
	long waited;

	if (!count)
		return 0;
	if (count < sizeof(struct edu_event) ||
	    count % sizeof(struct edu_event))
		return -EINVAL;
	max_events = min_t(size_t, count / sizeof(struct edu_event),
			   EDU_READ_BATCH);

	if (mutex_lock_interruptible(&dev->read_lock))
		return -ERESTARTSYS;

	for (;;) {
		if (!edu_events_ready(dev)) {
			if (READ_ONCE(dev->dead)) {
				ret = 0;
				goto out_unlock;
			}
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				goto out_unlock;
			}
			waited = wait_event_interruptible_timeout(dev->read_wait,
					edu_events_ready(dev) ||
					READ_ONCE(dev->dead),
					msecs_to_jiffies(EDU_READ_TIMEOUT_MS));
			if (waited < 0) {
				ret = waited;
				goto out_unlock;
			}
			if (!waited) {
				ret = -ETIMEDOUT;
				goto out_unlock;
			}
			continue;
		}

		spin_lock_irqsave(&dev->ring_lock, flags);
		copied = min_t(u32, max_events, dev->ring_count);
		for (i = 0; i < copied; i++)
			events[i] = dev->ring[(dev->ring_tail + i) %
					      EDU_EVENT_CAPACITY];
		spin_unlock_irqrestore(&dev->ring_lock, flags);

		if (copy_to_user(buffer, events,
				 copied * sizeof(struct edu_event))) {
			ret = -EFAULT;
			goto out_unlock;
		}

		spin_lock_irqsave(&dev->ring_lock, flags);
		dev->ring_tail = (dev->ring_tail + copied) % EDU_EVENT_CAPACITY;
		dev->ring_count -= copied;
		spin_unlock_irqrestore(&dev->ring_lock, flags);
		ret = copied * sizeof(struct edu_event);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&dev->read_lock);
	return ret;
}

static __poll_t edu_poll(struct file *file, poll_table *wait)
{
	struct edu_device *dev = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &dev->read_wait, wait);
	if (edu_events_ready(dev))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (READ_ONCE(dev->dead))
		mask |= EPOLLHUP;
	return mask;
}

static long edu_ioctl(struct file *file, unsigned int command,
		      unsigned long argument)
{
	struct edu_device *dev = file->private_data;
	void __user *user_argument = (void __user *)argument;
	long ret;

	if (READ_ONCE(dev->dead))
		return -ENODEV;
	if (_IOC_TYPE(command) != EDU_IOCTL_MAGIC)
		return -ENOTTY;

	switch (command) {
	case EDU_IOC_GET_INFO: {
		struct edu_info info;

		if (mutex_lock_interruptible(&dev->operation_lock))
			return -ERESTARTSYS;
		if (READ_ONCE(dev->dead)) {
			mutex_unlock(&dev->operation_lock);
			return -ENODEV;
		}
		info.vendor_id = EDU_VENDOR_ID;
		info.device_id = EDU_DEVICE_ID;
		info.device_version = readl(dev->bar0 + EDU_REG_IDENTIFICATION);
		info.bar0_size = min_t(resource_size_t, dev->bar0_len, U32_MAX);
		info.irq_mode = dev->irq_mode;
		info.event_capacity = EDU_EVENT_CAPACITY;
		mutex_unlock(&dev->operation_lock);
		return copy_to_user(user_argument, &info, sizeof(info)) ?
			-EFAULT : 0;
	}
	case EDU_IOC_INVERT: {
		struct edu_invert request;

		if (copy_from_user(&request, user_argument, sizeof(request)))
			return -EFAULT;
		if (mutex_lock_interruptible(&dev->operation_lock))
			return -ERESTARTSYS;
		if (READ_ONCE(dev->dead)) {
			ret = -ENODEV;
		} else {
			writel(request.input, dev->bar0 + EDU_REG_INVERT);
			request.result = readl(dev->bar0 + EDU_REG_INVERT);
			ret = 0;
		}
		mutex_unlock(&dev->operation_lock);
		if (ret)
			return ret;
		return copy_to_user(user_argument, &request, sizeof(request)) ?
			-EFAULT : 0;
	}
	case EDU_IOC_FACTORIAL: {
		struct edu_factorial request;

		if (copy_from_user(&request, user_argument, sizeof(request)))
			return -EFAULT;
		ret = edu_do_factorial(dev, &request);
		if (ret)
			return ret;
		return copy_to_user(user_argument, &request, sizeof(request)) ?
			-EFAULT : 0;
	}
	case EDU_IOC_DMA: {
		struct edu_dma_request request;

		if (copy_from_user(&request, user_argument, sizeof(request)))
			return -EFAULT;
		return edu_do_dma(dev, &request);
	}
	case EDU_IOC_GET_STATS: {
		struct edu_stats stats;
		unsigned long flags;

		stats.irq_count = atomic64_read(&dev->irq_count);
		stats.factorial_count = atomic64_read(&dev->factorial_count);
		stats.dma_count = atomic64_read(&dev->dma_count);
		spin_lock_irqsave(&dev->ring_lock, flags);
		stats.event_overflows = dev->event_overflows;
		stats.last_sequence = dev->last_sequence;
		spin_unlock_irqrestore(&dev->ring_lock, flags);
		return copy_to_user(user_argument, &stats, sizeof(stats)) ?
			-EFAULT : 0;
	}
	default:
		return -ENOTTY;
	}
}

static const struct file_operations edu_fops = {
	.owner = THIS_MODULE,
	.open = edu_open,
	.release = edu_release,
	.read = edu_read,
	.poll = edu_poll,
	.unlocked_ioctl = edu_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = no_llseek,
};

static struct miscdevice edu_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "edupci",
	.fops = &edu_fops,
	.mode = 0600,
};

static int edu_setup_irq(struct edu_device *dev)
{
	int ret;

	ret = pci_alloc_irq_vectors(dev->pdev, 1, 1, PCI_IRQ_MSI);
	if (ret == 1) {
		dev->irq_mode = EDU_IRQ_MODE_MSI;
	} else {
		ret = pci_alloc_irq_vectors(dev->pdev, 1, 1, PCI_IRQ_LEGACY);
		if (ret < 0)
			return ret;
		dev->irq_mode = EDU_IRQ_MODE_INTX;
	}
	dev->irq_vectors_allocated = true;
	dev->irq = pci_irq_vector(dev->pdev, 0);
	if (dev->irq < 0)
		return dev->irq;

	ret = request_irq(dev->irq, edu_irq_handler,
			  dev->irq_mode == EDU_IRQ_MODE_INTX ? IRQF_SHARED : 0,
			  "edupci", dev);
	if (ret)
		return ret;
	dev->irq_requested = true;
	return 0;
}

static void edu_free_resources(struct edu_device *dev)
{
	if (dev->bus_master_enabled) {
		pci_clear_master(dev->pdev);
		dev->bus_master_enabled = false;
	}
	if (dev->irq_requested) {
		free_irq(dev->irq, dev);
		dev->irq_requested = false;
	}
	if (dev->irq_vectors_allocated) {
		pci_free_irq_vectors(dev->pdev);
		dev->irq_vectors_allocated = false;
	}
	if (dev->dma_cpu) {
		dma_free_coherent(&dev->pdev->dev, EDU_DMA_BUFFER_SIZE,
				  dev->dma_cpu, dev->dma_address);
		dev->dma_cpu = NULL;
	}
	if (dev->bar0) {
		pci_iounmap(dev->pdev, dev->bar0);
		dev->bar0 = NULL;
	}
	if (dev->regions_requested) {
		pci_release_regions(dev->pdev);
		dev->regions_requested = false;
	}
	if (dev->device_enabled) {
		pci_disable_device(dev->pdev);
		dev->device_enabled = false;
	}
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct edu_device *dev;
	resource_size_t bar_start;
	u32 identification;
	int ret;

	mutex_lock(&edu_singleton_lock);
	if (edu_claimed) {
		mutex_unlock(&edu_singleton_lock);
		return -EBUSY;
	}
	edu_claimed = true;
	mutex_unlock(&edu_singleton_lock);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err_claim;
	}
	kref_init(&dev->refcount);
	dev->pdev = pdev;
	mutex_init(&dev->operation_lock);
	mutex_init(&dev->read_lock);
	spin_lock_init(&dev->ring_lock);
	init_waitqueue_head(&dev->read_wait);
	init_completion(&dev->factorial_done);
	init_completion(&dev->dma_done);
	atomic64_set(&dev->irq_count, 0);
	atomic64_set(&dev->factorial_count, 0);
	atomic64_set(&dev->dma_count, 0);

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_put;
	dev->device_enabled = true;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		ret = -ENODEV;
		goto err_resources;
	}
	dev->bar0_len = pci_resource_len(pdev, 0);
	if (dev->bar0_len < EDU_REG_DMA_COMMAND + sizeof(u32)) {
		ret = -ENODEV;
		goto err_resources;
	}
	ret = pci_request_regions(pdev, "edupci");
	if (ret)
		goto err_resources;
	dev->regions_requested = true;

	dev->bar0 = pci_iomap(pdev, 0, 0);
	if (!dev->bar0) {
		ret = -ENOMEM;
		goto err_resources;
	}
	identification = readl(dev->bar0 + EDU_REG_IDENTIFICATION);
	if ((identification & 0xff) != 0xed) {
		dev_err(&pdev->dev, "unexpected EDU identification 0x%08x\n",
			identification);
		ret = -ENODEV;
		goto err_resources;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(&pdev->dev, "cannot set EDU's 28-bit DMA mask\n");
		goto err_resources;
	}
	dev->dma_cpu = dma_alloc_coherent(&pdev->dev, EDU_DMA_BUFFER_SIZE,
					  &dev->dma_address, GFP_KERNEL);
	if (!dev->dma_cpu) {
		ret = -ENOMEM;
		goto err_resources;
	}
	if (upper_32_bits(dev->dma_address)) {
		ret = -ERANGE;
		goto err_resources;
	}
	pci_set_master(pdev);
	dev->bus_master_enabled = true;

	writel(0, dev->bar0 + EDU_REG_STATUS);
	edu_ack_pending_irqs(dev);
	ret = edu_setup_irq(dev);
	if (ret)
		goto err_resources;

	mutex_lock(&edu_singleton_lock);
	edu_singleton = dev;
	ret = misc_register(&edu_miscdevice);
	if (ret)
		edu_singleton = NULL;
	mutex_unlock(&edu_singleton_lock);
	if (ret)
		goto err_resources;

	pci_set_drvdata(pdev, dev);
	bar_start = pci_resource_start(pdev, 0);
	dev_info(&pdev->dev,
		 "QEMU EDU ready: BAR0=%pa size=%pa IRQ=%s (%d) DMA=%pad\n",
		 &bar_start, &dev->bar0_len,
		 dev->irq_mode == EDU_IRQ_MODE_MSI ? "MSI" : "INTx",
		 dev->irq, &dev->dma_address);
	return 0;

err_resources:
	edu_free_resources(dev);
err_put:
	kref_put(&dev->refcount, edu_device_release);
err_claim:
	mutex_lock(&edu_singleton_lock);
	edu_claimed = false;
	mutex_unlock(&edu_singleton_lock);
	return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu_device *dev = pci_get_drvdata(pdev);
	u32 causes;

	if (!dev)
		return;

	mutex_lock(&edu_singleton_lock);
	WRITE_ONCE(dev->dead, true);
	if (edu_singleton == dev)
		edu_singleton = NULL;
	misc_deregister(&edu_miscdevice);
	mutex_unlock(&edu_singleton_lock);

	wake_up_interruptible_all(&dev->read_wait);
	complete_all(&dev->factorial_done);
	complete_all(&dev->dma_done);

	/* EDU has no DMA cancel register; wait for its bounded transfer engine. */
	mutex_lock(&dev->operation_lock);
	if (dev->dma_cpu &&
	    edu_wait_dma_idle(dev, EDU_OPERATION_TIMEOUT_MS)) {
		dev_warn(&pdev->dev, "DMA still active during remove\n");
		dev->engine_faulted = true;
		if (dev->bus_master_enabled) {
			pci_clear_master(pdev);
			dev->bus_master_enabled = false;
		}
		pci_intx(pdev, 0);
		dev_warn(&pdev->dev,
			 "retaining coherent DMA buffer because EDU may still use it\n");
		dev->dma_cpu = NULL;
	}
	writel(0, dev->bar0 + EDU_REG_STATUS);
	causes = readl(dev->bar0 + EDU_REG_IRQ_STATUS);
	if (causes)
		writel(causes, dev->bar0 + EDU_REG_IRQ_ACK);
	synchronize_irq(dev->irq);
	mutex_unlock(&dev->operation_lock);

	dev_info(&pdev->dev, "removing EduPCI device\n");
	edu_free_resources(dev);
	pci_set_drvdata(pdev, NULL);

	mutex_lock(&edu_singleton_lock);
	edu_claimed = false;
	mutex_unlock(&edu_singleton_lock);
	kref_put(&dev->refcount, edu_device_release);
}

static const struct pci_device_id edu_pci_ids[] = {
	{ PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, edu_pci_ids);

static struct pci_driver edu_pci_driver = {
	.name = "edupci",
	.id_table = edu_pci_ids,
	.probe = edu_probe,
	.remove = edu_remove,
};
module_pci_driver(edu_pci_driver);

MODULE_AUTHOR("EduPCI project");
MODULE_DESCRIPTION("Linux PCI driver for QEMU EDU virtual hardware");
MODULE_LICENSE("GPL");
