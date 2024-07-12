// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based on virtio_mmio.c
 *   Copyright 2011-2014, ARM Ltd.
 *
 * Copyright 2022-2024 Virtual Open Systems SAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) "virtio-loopback: " fmt

#include <linux/cdev.h>
#include <linux/eventfd.h>
#include <linux/fdtable.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/slab.h>

/* Virtio-loopback includes */
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_mmio.h>
#include <linux/virtio_ring.h>
#include <linux/pid.h>
#include <linux/kthread.h>

/* Loopback header file */
#include "loopback_driver.h"

/* Features */
MODULE_LICENSE("GPL v2");

/* function declaration */
static uint64_t read_adapter(uint64_t fn_id, uint64_t size);
static void write_adapter(uint64_t data, uint64_t fn_id, uint64_t size);

struct mmap_info *info;
int mmap_index;
uint64_t sum_pgfaults;

/* Waitq */
wait_queue_head_t wq;
wait_queue_head_t wq_notify;

/* Read write mutex */
struct mutex read_write_lock;
struct mutex interrupt_lock;

/* Notification spinlock */
spinlock_t notify_q_spinlock;

bool share_communication_struct;
uint32_t vq_index;
uint64_t vq_pfns[16], vq_pfn;

struct virtio_mmio_device *vm_dev_irq;

struct virtqueue *global_vq;
const struct vring *global_vring;

/* counters */
static int interrupt_cnt;
static int notify_sent, notify_received;

/* Define a notification list */
static struct list_head *notify_list;

static struct platform_device *virtio_loopback_device;

/* Virio-loopback device funcitonality */
struct eventfd_ctx *efd_ctx;
static struct task_struct *start_loopback_thread;
static struct task_struct *start_notification_thread;

/* global storage for device Major number */
static int dev_major;
/* sysfs class structure */
static struct class *loopback_class;
/* array of loopback_device_data for */
static struct loopback_device_data *loopback_data;

/* Allow only one process to open the driver */
unsigned long loopback_flags;

/* Current ram index */
int cur_ram_idx;

/*
 * If this variable is true then read/write should wait
 * the adapter to unlock this opertion by sending an
 * eventfd. If it's equal to "false" then the oparetion
 * does not wait for adapter's confirmation.
 */
bool valid_eventfd;

/* Create a mapping array */
struct share_mmap share_mmap_list[MMAP_LIMIT];

/* Configuration interface */

static u64 vm_get_features(struct virtio_device *vdev)
{
	u64 features;

	/* Take feature bits 0-31 */
	write_adapter(1, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 4);
	features = read_adapter(VIRTIO_MMIO_DEVICE_FEATURES, 4);
	features <<= 32;

	/* Take feature bits 32-63 */
	write_adapter(0, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 4);
	features |= read_adapter(VIRTIO_MMIO_DEVICE_FEATURES, 4);

	return features;
}

static int vm_finalize_features(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Make sure there are no mixed devices */
	if (vm_dev->version == 2 &&
		!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "New virtio-mmio devices (version 2) "
				"must provide VIRTIO_F_VERSION_1 feature!\n");
		return -EINVAL;
	}

	write_adapter(1, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 4);
	write_adapter((u32)(vdev->features >> 32), VIRTIO_MMIO_DRIVER_FEATURES, 4);

	write_adapter(0, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 4);
	write_adapter((u32)vdev->features, VIRTIO_MMIO_DRIVER_FEATURES, 4);

	return 0;
}

static void vm_get(struct virtio_device *vdev, unsigned int offset,
				   void *buf, unsigned int len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			ptr[i] = read_adapter(VIRTIO_MMIO_CONFIG + offset + i, 1);
		return;
	}

	switch (len) {
	case 1:
		b = read_adapter(VIRTIO_MMIO_CONFIG + offset, 1);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = cpu_to_le16(read_adapter(VIRTIO_MMIO_CONFIG + offset, 2));
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = cpu_to_le32(read_adapter(VIRTIO_MMIO_CONFIG + offset, 4));
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = cpu_to_le32(read_adapter(VIRTIO_MMIO_CONFIG + offset, 4));
		memcpy(buf, &l, sizeof(l));
		l = cpu_to_le32(read_adapter(VIRTIO_MMIO_CONFIG + offset + sizeof(l), 4));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		BUG();
	}
}

static void vm_set(struct virtio_device *vdev, unsigned int offset,
				   const void *buf, unsigned int len)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	u8 b;
	__le16 w;
	__le32 l;

	if (vm_dev->version == 1) {
		const u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			write_adapter(ptr[i], VIRTIO_MMIO_CONFIG + offset + i, 1);

		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		write_adapter(b, VIRTIO_MMIO_CONFIG + offset, 1);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		write_adapter(le16_to_cpu(w), VIRTIO_MMIO_CONFIG + offset, 2);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		write_adapter(le32_to_cpu(l), VIRTIO_MMIO_CONFIG + offset, 4);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		write_adapter(le32_to_cpu(l), VIRTIO_MMIO_CONFIG + offset, 4);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		write_adapter(le32_to_cpu(l), VIRTIO_MMIO_CONFIG + offset + sizeof(l), 4);
		break;
	default:
		BUG();
	}
}

static u32 vm_generation(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	if (vm_dev->version == 1)
		return 0;
	else
		return read_adapter(VIRTIO_MMIO_CONFIG_GENERATION, 4);
}

static u8 vm_get_status(struct virtio_device *vdev)
{
	return read_adapter(VIRTIO_MMIO_STATUS, 4) & 0xff;
}

static void vm_set_status(struct virtio_device *vdev, u8 status)
{
	write_adapter(status, VIRTIO_MMIO_STATUS, 4);
}

static void vm_reset(struct virtio_device *vdev)
{
	/* 0 status means a reset. */
	write_adapter(0, VIRTIO_MMIO_STATUS, 4);
}

int start_notification(void *data)
{
	struct notify_data *first_notification;
	uint32_t index;

	DBG("Start notification\n");

	(void)data;

	while(1) {

		spin_lock(&notify_q_spinlock);
		while (valid_eventfd && list_empty(notify_list) == 1) {
			spin_unlock(&notify_q_spinlock);
			wait_event_timeout(wq_notify, list_empty(notify_list) != 1, 1 * HZ);
			spin_lock(&notify_q_spinlock);
		}

		first_notification = list_first_entry(notify_list, struct notify_data, list);
		index = first_notification->index;
		list_del(&first_notification->list);

		DBG("notify_received: %d, VQ: %d\n", notify_received++, index);
		spin_unlock(&notify_q_spinlock);
		write_adapter(index, VIRTIO_MMIO_QUEUE_NOTIFY, 4);

		if (!valid_eventfd) {
			DBG("Exit notification thread\n");
			return 0;
		}
	}
}

/* the notify function used when creating a virt queue */
static bool vm_notify(struct virtqueue *vq)
{
	struct notify_data *data;
	/*
	 * We write the queue's selector into
	 * the notification register to signal
	 * the other end
	 */
	spin_lock(&notify_q_spinlock);
	DBG("vm_notify\n");

	data = kmalloc(sizeof(struct notify_data), GFP_KERNEL);
	data->index = vq->index;
	INIT_LIST_HEAD(&data->list);
	list_add_tail(&data->list, notify_list);
	spin_unlock(&notify_q_spinlock);

	wake_up(&wq_notify);

	return true;
}

/* Notify all virtqueues on an interrupt. */
static void vm_interrupt(struct work_struct *work)
{
	struct virtio_mmio_device *vm_dev = vm_dev_irq;
	struct virtio_mmio_vq_info *info;
	int irq = 44;
	unsigned long status;

	/* STATUS and ACK should ne done without any intermediate status change */
	mutex_lock(&interrupt_lock);
	DBG("interrupt_cnt: %d\n", interrupt_cnt++);

	/* Read and acknowledge interrupts */
	status = read_adapter(VIRTIO_MMIO_INTERRUPT_STATUS, 4);
	write_adapter(status, VIRTIO_MMIO_INTERRUPT_ACK, 4);

	if (unlikely(status & VIRTIO_MMIO_INT_CONFIG))
		virtio_config_changed(&vm_dev->vdev);

	if (likely(status & VIRTIO_MMIO_INT_VRING)) {
		spin_lock(&vm_dev->lock);
		list_for_each_entry(info, &vm_dev->virtqueues, node) {
			(void)vring_interrupt(irq, info->vq);
		}
		spin_unlock(&vm_dev->lock);
	}
	mutex_unlock(&interrupt_lock);
}

static void vm_del_vq(struct virtqueue *vq)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vq->vdev);
	struct virtio_mmio_vq_info *info = vq->priv;
	unsigned long flags;
	unsigned int index = vq->index;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	/* Select and deactivate the queue */
	write_adapter(index, VIRTIO_MMIO_QUEUE_SEL, 4);

	if (vm_dev->version == 1) {
		write_adapter(0, VIRTIO_MMIO_QUEUE_PFN, 4);
	} else {
		write_adapter(0, VIRTIO_MMIO_QUEUE_READY, 4);
		WARN_ON(read_adapter(VIRTIO_MMIO_QUEUE_READY, 4));
	}

	vring_del_virtqueue(vq);
	kfree(info);
}

static void vm_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		vm_del_vq(vq);
}

static struct virtqueue *vm_setup_vq(struct virtio_device *vdev, unsigned int index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name, bool ctx)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct virtio_mmio_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags;
	unsigned int num;
	int err;

	if (!name)
		return NULL;

	/* Select the queue we're interested in */
	write_adapter(index, VIRTIO_MMIO_QUEUE_SEL, 4);

	/* Queue shouldn't already be set up. */
	if (read_adapter((vm_dev->version == 1 ?
			VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY), 4)) {
		err = -ENOENT;
		goto error_available;
	}

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto error_kmalloc;
	}

	num = read_adapter(VIRTIO_MMIO_QUEUE_NUM_MAX, 4);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
					 true, true, ctx, vm_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}


#if LINUX_VERSION_CODE >  KERNEL_VERSION(6,0,0)
	vq->num_max = num;
#endif
	/* Activate the queue */
	write_adapter(virtqueue_get_vring_size(vq), VIRTIO_MMIO_QUEUE_NUM, 4);
	if (vm_dev->version == 1) {
		u64 q_pfn = virtqueue_get_desc_addr(vq);

		q_pfn = q_pfn >> PAGE_SHIFT;

		/* Copy the physical address and enable the mmap */
		vq_pfn = q_pfn;
		vq_pfns[vq_index++] = q_pfn;

		/* Save the virtqueue in a global variable */
		global_vq = vq;
		global_vring = virtqueue_get_vring(vq);

		/*
		 * virtio-mmio v1 uses a 32bit QUEUE PFN. If we have something
		 * that doesn't fit in 32bit, fail the setup rather than
		 * pretending to be successful.
		 */
		if (q_pfn >> 32) {
			dev_err(&vdev->dev,
				"platform bug: legacy virtio-mmio must not "
				"be used with RAM above 0x%llxGB\n",
				0x1ULL << (32 + PAGE_SHIFT - 30));
			err = -E2BIG;
			goto error_bad_pfn;
		}

		write_adapter(PAGE_SIZE, VIRTIO_MMIO_QUEUE_ALIGN, 4);
		write_adapter(q_pfn, VIRTIO_MMIO_QUEUE_PFN, 4);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_DESC_LOW, 4);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_DESC_HIGH, 4);

		addr = virtqueue_get_avail_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_AVAIL_LOW, 4);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_AVAIL_HIGH, 4);

		addr = virtqueue_get_used_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_USED_LOW, 4);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_USED_HIGH, 4);

		write_adapter(1, VIRTIO_MMIO_QUEUE_READY, 4);
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_add(&info->node, &vm_dev->virtqueues);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	return vq;

error_bad_pfn:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	if (vm_dev->version == 1) {
		write_adapter(0, VIRTIO_MMIO_QUEUE_PFN, 4);
	} else {
		write_adapter(0, VIRTIO_MMIO_QUEUE_READY, 4);
		WARN_ON(read_adapter(VIRTIO_MMIO_QUEUE_READY, 4));
	}
	kfree(info);
error_kmalloc:
error_available:
	return ERR_PTR(err);
}

static int vm_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
					   struct virtqueue *vqs[],
					   vq_callback_t *callbacks[],
					   const char * const names[],
					   const bool *ctx,
					   struct irq_affinity *desc)
{
	int i, queue_idx = 0;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = vm_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
							 ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			vm_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}

static const char *vm_bus_name(struct virtio_device *vdev)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);

	return vm_dev->pdev->name;
}

static bool vm_get_shm_region(struct virtio_device *vdev,
				  struct virtio_shm_region *region, u8 id)
{
	u64 len, addr;

	/* Select the region we're interested in */
	write_adapter(id, VIRTIO_MMIO_SHM_SEL, 4);

	/* Read the region size */
	len = (u64) read_adapter(VIRTIO_MMIO_SHM_LEN_LOW, 4);
	len |= (u64) read_adapter(VIRTIO_MMIO_SHM_LEN_HIGH, 4) << 32;

	region->len = len;

	/* Check if region length is -1. If that's the case, the shared memory
	 * region does not exist and there is no need to proceed further.
	 */
	if (len == ~(u64)0)
		return false;

	/* Read the region base address */
	addr = (u64) read_adapter(VIRTIO_MMIO_SHM_BASE_LOW, 4);
	addr |= (u64) read_adapter(VIRTIO_MMIO_SHM_BASE_HIGH, 4) << 32;

	region->addr = addr;

	return true;
}

static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get			= vm_get,
	.set			= vm_set,
	.generation		= vm_generation,
	.get_status		= vm_get_status,
	.set_status		= vm_set_status,
	.reset			= vm_reset,
	.find_vqs		= vm_find_vqs,
	.del_vqs		= vm_del_vqs,
	.get_features		= vm_get_features,
	.finalize_features	= vm_finalize_features,
	.bus_name		= vm_bus_name,
	.get_shm_region		= vm_get_shm_region,
};

static void virtio_mmio_release_dev(struct device *_d)
{
	struct virtio_device *vdev = container_of(_d, struct virtio_device, dev);
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct platform_device *pdev = vm_dev->pdev;

	devm_kfree(&pdev->dev, vm_dev);
}

static int virtio_mmio_probe(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev;
	unsigned long magic;
	int rc;

	vm_dev = devm_kzalloc(&pdev->dev, sizeof(*vm_dev), GFP_KERNEL);
	if (!vm_dev)
		return -ENOMEM;

	/* Save the device pointer globally */
	vm_dev_irq = vm_dev;

	vm_dev->vdev.dev.parent = &pdev->dev;
	vm_dev->vdev.dev.release = virtio_mmio_release_dev;
	vm_dev->vdev.config = &virtio_mmio_config_ops;
	vm_dev->pdev = pdev;
	INIT_LIST_HEAD(&vm_dev->virtqueues);
	spin_lock_init(&vm_dev->lock);

	/* Check magic value */
	magic = read_adapter(VIRTIO_MMIO_MAGIC_VALUE, 4);

	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}

	/* Check device version */
	vm_dev->version = read_adapter(VIRTIO_MMIO_VERSION, 4);

	if (vm_dev->version < 1 || vm_dev->version > 2) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
							vm_dev->version);
		return -ENXIO;
	}

	vm_dev->vdev.id.device = read_adapter(VIRTIO_MMIO_DEVICE_ID, 4);

	if (vm_dev->vdev.id.device == 0) {
		/*
		 * virtio-mmio device with an ID 0 is a (dummy) placeholder
		 * with no function. End probing now with no error reported.
		 */
		return -ENODEV;
	}

	vm_dev->vdev.id.vendor = read_adapter(VIRTIO_MMIO_VENDOR_ID, 4);

	if (vm_dev->version == 1) {
		write_adapter(PAGE_SIZE, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4);

		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
		/*
		 * In the legacy case, ensure our coherently-allocated virtio
		 * ring will be at an address expressable as a 32-bit PFN.
		 */
		if (!rc)
			dma_set_coherent_mask(&pdev->dev,
				  DMA_BIT_MASK(32 + PAGE_SHIFT));
	} else {
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	}
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		dev_warn(&pdev->dev, "Failed to enable 64-bit or 32-bit DMA."
				"Trying to continue, but this might not work.\n");

	platform_set_drvdata(pdev, vm_dev);

	rc = register_virtio_device(&vm_dev->vdev);

	if (rc)
		put_device(&vm_dev->vdev.dev);

	return rc;
}

static int virtio_mmio_remove(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev = platform_get_drvdata(pdev);

	unregister_virtio_device(&vm_dev->vdev);
	DBG("unregister_virtio_device!\n");
	return 0;
}

/* Not need of DTS and ACPI */
static struct platform_driver virtio_mmio_driver = {
	.probe	 = virtio_mmio_probe,
	.remove	 = virtio_mmio_remove,
	.driver	 = {
		.name   = "loopback-transport",
	},
};

void pf_mmap_close(struct vm_area_struct *vma)
{
	DBG("unmap\t-> vma->vm_start: 0x%lx\n", vma->vm_start);
	DBG("unmap\t-> size: %lu\n", vma->vm_end - vma->vm_start);
	share_mmap_rem(vma, share_mmap_list);
}

vm_fault_t pf_mmap_fault(struct vm_fault *vmf)
{
	uint64_t corrected_pfn;
	pfn_t corr_pfn_struct;
	struct page *page;
	int ret = 0;

	DBG("----- Page fault: %lld -----\n", sum_pgfaults++);

	/* Find the corrected pfn */
	corrected_pfn = share_mmap_exist_vma_return_correct_pfn(vmf->address, share_mmap_list);
	corr_pfn_struct.val = corrected_pfn;

	/* Some debug prints */
	DBG("vma->vm_start: 0x%lx\n", vmf->vma->vm_start);
	DBG("vma->vm_pgoff: 0x%lx\n", vmf->vma->vm_pgoff);
	DBG("vmf->address: 0x%lx\n", vmf->address);
	DBG("corrected_pfn: 0x%llx\n", corrected_pfn);
	DBG("pfn_valid(corrected_pfn): 0x%x\n", pfn_valid(corrected_pfn));

	BUG_ON(!pfn_valid(corrected_pfn));

	/* After finding the page, correct the vmf->page */
	page = pfn_to_page(corrected_pfn);
	BUG_ON(!virt_addr_valid(page_address(page)));

	/* Insert the correct page */
	ret = vmf_insert_pfn(vmf->vma, vmf->address, corrected_pfn);
	DBG("vmf_insert_pfn -> ret: %d\n", ret);

	return ret;
}

const struct vm_operations_struct pf_mmap_ops = {
	.close = pf_mmap_close,
	.fault = pf_mmap_fault,
};

int pf_mmap_vm_page(struct file *filp, struct vm_area_struct *vma)
{
	uint64_t size = (unsigned long)(vma->vm_end - vma->vm_start);
	uint64_t pfn = ((cur_ram_idx++) * 0x40000);

#if LINUX_VERSION_CODE <  KERNEL_VERSION(6,3,0)
	vma->vm_flags |= VM_PFNMAP;
#else
	vm_flags_set(vma, VM_PFNMAP);
#endif
	add_share_mmap(filp, pfn, vma->vm_start, size, share_mmap_list, &mmap_index);
	return 0;
}

int op_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	uint64_t size = (unsigned long)(vma->vm_end - vma->vm_start);

	DBG("op_mmap -> vma->vm_pgoff: 0x%lx", vma->vm_pgoff);

	if (share_communication_struct) {
		DBG("MMAP communication struct\n");
		ret = mmap_communication_shared_space(filp, vma, share_mmap_list, &mmap_index);
		share_communication_struct = false;
		goto out;
	}

	if (size > PAGE_SIZE * 100) {
		ret = pf_mmap_vm_page(filp, vma);
		goto out;
	}

	ret = mmap_mix(filp, vma, share_mmap_list, &mmap_index, vq_pfn);

out:
	vma->vm_ops = &pf_mmap_ops;
	return ret;
}

static uint64_t read_adapter(uint64_t fn_id, uint64_t size)
{
	uint64_t result;

	mutex_lock(&read_write_lock);

	/*
	 * By enabling the following line all
	 * read messages will be printed:
	 *
	 * print_neg_flag(fn_id, 1);
	 */
	print_neg_flag(fn_id, 1);

	((virtio_neg_t *)(info->data))->notification = fn_id;
	((virtio_neg_t *)(info->data))->data = 0;
	((virtio_neg_t *)(info->data))->size = size;
	((virtio_neg_t *)(info->data))->read = true;

	atomic_set(&((virtio_neg_t *)(info->data))->done, 0);

	eventfd_signal(efd_ctx, 1);

	/*
	 * There is a chance virtio-loopback adapter to call "wake_up"
	 * before the current thread sleep. This is the reason that
	 * "wait_event_timeout" is used instead of "wait_event". In this
	 * way, virtio-loopback driver will wake up even if has missed the
	 * "wake_up" kick, check the updated "done" value and return.
	 */

	while (valid_eventfd && atomic_read(&((virtio_neg_t *)(info->data))->done) != 1)
		wait_event_timeout(wq, atomic_read(&((virtio_neg_t *)(info->data))->done) == 1, 1 * HZ);

	result = ((virtio_neg_t *)(info->data))->data;

	mutex_unlock(&read_write_lock);

	return result;
}

static void write_adapter(uint64_t data, uint64_t fn_id, uint64_t size)
{

	mutex_lock(&read_write_lock);

	/*
	 * By enabling the following line all
	 * write messages will be printed:
	 *
	 * print_neg_flag(fn_id, 1);
	 */
	print_neg_flag(fn_id, 0);

	((virtio_neg_t *)(info->data))->notification = fn_id;
	((virtio_neg_t *)(info->data))->data = data;
	((virtio_neg_t *)(info->data))->size = size;
	((virtio_neg_t *)(info->data))->read = false;

	atomic_set(&((virtio_neg_t *)(info->data))->done, 0);

	eventfd_signal(efd_ctx, 1);

	/*
	 * There is a chance virtio-loopback adapter to call "wake_up"
	 * before the current thread sleep. This is the reason that
	 * "wait_event_timeout" is used instead of "wait_event". In this
	 * way, virtio-loopback driver will wake up even if has missed the
	 * "wake_up" kick, check the updated "done" value and return.
	 */
	while (valid_eventfd && atomic_read(&((virtio_neg_t *)(info->data))->done) != 1)
		wait_event_timeout(wq, atomic_read(&((virtio_neg_t *)(info->data))->done) == 1, 1 * HZ);

	mutex_unlock(&read_write_lock);
}

/* Defined for future work */
static ssize_t loopback_write(struct file *file,
				   const char __user *user_buffer,
				   size_t size,
				   loff_t *offset)
{
	ssize_t len = sizeof(int);

	DBG("loopback write function is called\n");
	if (len <= 0)
		return 0;

	return len;
}

/* Defined for future work */
static ssize_t loopback_read(struct file *file,
				 char __user *user_buffer,
				 size_t size, loff_t *offset)
{
	DBG("loopback read function is called\n");
	return 0;
}

loff_t loopback_seek(struct file *file, loff_t offset, int whence)
{
	loff_t new_pos;

	DBG("loopback seek function!\n");
	switch (whence) {
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = file->f_pos + offset;
		break;
	case SEEK_END:
		new_pos = file->f_inode->i_size;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0 || new_pos > file->f_inode->i_size)
		return -EINVAL;

	return new_pos;
}

int start_loopback(void *data)
{
	(void)data;
	/* Register mmio_trasmport */
	(void)platform_driver_register(&virtio_mmio_driver);
	return 0;
}

static long loopback_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	efd_data_t efd_data;
	struct task_struct *userspace_task;
	struct file *efd_file;
	int irq;
	uint32_t queue_sel;

	switch (cmd) {
	case EFD_INIT:
		if (copy_from_user(&efd_data, (efd_data_t *) arg,
						   sizeof(efd_data_t)))
			return -EFAULT;

		userspace_task = pid_task(find_vpid(efd_data.pid), PIDTYPE_PID);

		rcu_read_lock();

#if LINUX_VERSION_CODE <  KERNEL_VERSION(5,11,220)
		efd_file = fcheck_files(userspace_task->files, efd_data.efd[0]);
#else
		efd_file = files_lookup_fd_rcu(userspace_task->files, efd_data.efd[0]);
#endif

		rcu_read_unlock();

		efd_ctx = eventfd_ctx_fileget(efd_file);
		if (!efd_ctx)
			return -1;

		break;
	case WAKEUP:
		atomic_set(&((virtio_neg_t *)(info->data))->done, 1);
		wake_up(&wq);
		break;
	case START_LOOPBACK:
		start_notification_thread = kthread_run(start_notification, NULL, "start_notification");
		start_loopback_thread = kthread_run(start_loopback, NULL, "start_loopback");
		break;
	case IRQ:
		if (copy_from_user(&irq, (int *) arg, sizeof(int)))
			return -EFAULT;
		DBG("\nIRQ\n");
		/*
		 * Both of the interrupt ways work but a) is more stable
		 * and b) has better performance:
		 * a) vm_interrupt(NULL);
		 * b) queue_work(interrupt_workqueue, &async_interrupt);
		 */
		vm_interrupt(NULL);
		break;
	case SHARE_VQS:
		if (copy_from_user(&queue_sel, (uint32_t *) arg, sizeof(uint32_t)))
			return -EFAULT;
		DBG("\n\nSHARE_VQS: %u\n\n", queue_sel);
		vq_pfn = vq_pfns[queue_sel];
		DBG("Selected pfn is: 0x%llx", vq_pfn);
		break;
	case SHARE_COM_STRUCT:
		share_communication_struct = true;
		break;
	default:
		DBG("loopback ioctl: default, %u\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

/*
 * The current implementation of the driver supports
 * exclusive access to one user-space thread. Multi-device
 * support will be added in future implementation.
 */
static int loopback_open(struct inode *inode, struct file *file)
{
	uint32_t val_1gb = 1024 * 1024 * 1024; // 1GB
	virtio_neg_t device_neg = {.done = ATOMIC_INIT(0)};

	/* Update the global variable, the driver is in use */
	if (test_and_set_bit(IN_USE_BIT, &loopback_flags)) {
		DBG("Driver is busy\n");
		return -EBUSY;
	}

	/* Set the i_size for the stat SYS_CALL*/
	file->f_inode->i_size = 10 * val_1gb;

	/* Init mmap funcitonality */
	info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);
	info->data = (void *)get_zeroed_page(GFP_KERNEL);
	memcpy(info->data, &device_neg, sizeof(virtio_neg_t));

	/* assign this info struct to the file */
	file->private_data = info;

	/* Init notification list */
	notify_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
	if (!notify_list) {
		printk(KERN_ERR "Failed to allocate memory for list head\n");
		return 1;
	}
	INIT_LIST_HEAD(notify_list);

	/* Init global variables */
	mmap_index = 0;
	sum_pgfaults = 0;
	share_communication_struct = false;
	valid_eventfd = true;
	vq_index = 0;
	cur_ram_idx = 0;
	interrupt_cnt = 0;
	notify_sent = 0;
	notify_received = 0;

	return 0;
}

static int loopback_release(struct inode *inode, struct file *file)
{
	int i;

	DBG("Release the device\n");

	/*
	 * This make the read/write do not wait
	 * for the virtio-loopback-adapter if
	 * the last has closed the fd
	 */
	valid_eventfd = false;

	/* Unegister mmio_trasmport */
	platform_driver_unregister(&virtio_mmio_driver);
	DBG("platform_driver_unregister!\n");

	/* free communication structure */
	free_page((unsigned long)info->data);
	kfree(info);
	file->private_data = NULL;
	DBG("Clean private_data\n");

	/* Clear share_mmap_list */
	for(i = 0; i < MMAP_LIMIT; i++) {
		share_mmap_list[i].uid = 0;
		share_mmap_list[i].pfn = 0;
		share_mmap_list[i].vm_start = 0;
		share_mmap_list[i].size = 0;
	}

	/* Update the global variable, the driver is not in use */
	smp_mb__before_atomic();
	clear_bit(IN_USE_BIT, &loopback_flags);
	DBG("clear_bit!\n");

	return 0;
}

static const struct file_operations fops = {
	.owner			= THIS_MODULE,
	.read			= loopback_read,
	.write			= loopback_write,
	.open			= loopback_open,
	.unlocked_ioctl = loopback_ioctl,
	.mmap			= op_mmap,
	.llseek 		= loopback_seek,
	.release		= loopback_release
};

static int __init loopback_init(void)
{
	int err;
	dev_t dev;

	err = alloc_chrdev_region(&dev, 0, MAX_DEV, "loopback");
	dev_major = MAJOR(dev);

#if LINUX_VERSION_CODE <  KERNEL_VERSION(6,4,0)
	loopback_class = class_create(THIS_MODULE, "loopback");
#else
	loopback_class = class_create("loopback");
#endif
	if (IS_ERR(loopback_class)) {
			printk(KERN_ERR "Failed to create class\n");
			return PTR_ERR(loopback_class);
	}

	loopback_data = (struct loopback_device_data *)kmalloc(
						sizeof(struct loopback_device_data), GFP_KERNEL);

	cdev_init(&loopback_data->cdev, &fops);
	loopback_data->cdev.owner = THIS_MODULE;
	cdev_add(&loopback_data->cdev, MKDEV(dev_major, 0), 1);
	device_create(loopback_class, NULL, MKDEV(dev_major, 0), NULL, "loopback");

	/* Init wq */
	init_waitqueue_head(&wq);
	init_waitqueue_head(&wq_notify);

	/* Init mutex */
	mutex_init(&read_write_lock);
	mutex_init(&interrupt_lock);

	/* Init spinlock */
	spin_lock_init(&notify_q_spinlock);

	virtio_loopback_device = platform_device_register_simple("loopback-transport", -1, NULL, 0);
	if (IS_ERR(virtio_loopback_device)) {
		err = PTR_ERR(virtio_loopback_device);
		pr_err("failed to register loopback-transport device: %d\n", err);
		return err;
	}

	return 0;
}

void __exit loopback_exit(void)
{
	DBG("Exit driver!\n");

	/* Unegister loopback device */
	platform_device_unregister(virtio_loopback_device);
	DBG("platform_device_unregister!\n");

	device_destroy(loopback_class, MKDEV(dev_major, 0));
	cdev_del(&loopback_data->cdev);
	DBG("device_destroy!\n");

	class_destroy(loopback_class);
	DBG("class_destroy!\n");

	kfree(loopback_data);
	DBG("Free resources");
}

module_init(loopback_init);
module_exit(loopback_exit);
