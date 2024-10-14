// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio loopback transport driver
 *
 * Based on virtio_mmio.c
 *   Copyright 2011-2014, ARM Ltd.
 *
 * Copyright 2022-2024 Virtual Open Systems SAS
 *
 * Authors:
 *  Timos Ampelikiotis <t.ampelikiotis@virtualopensystems.com>
 *  Anna Panagopoulou <anna@virtualopensystems.com>
 *  Alvise Rigo <a.rigo@virtualopensystems.com>
 *
 * This module allows virtio devices to be used in a non-virtualized
 * environment, coupled with vhost-user device (user-space drivers).
 *
 * It is set as a transport driver by the virtio-loopback device
 * driver for a group of virtio drivers and reroutes all read/write
 * operations to the userspace. In user-space, virtio-loopback adapter
 * (the user-space component of the design) handles the read/write ops
 * translates them into the corresponding vhost-user messages and
 * forwards them to the corresponding vhost-user device.
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
 */

#define pr_fmt(fmt) "virtio-loopback-transport: " fmt

/* Loopback header file */
#include "virtio_loopback_driver.h"

static void print_neg_flag(uint64_t neg_flag, bool read)
{
	if (read)
		pr_debug("Read:\n");
	else
		pr_debug("Write:\n");

	switch (neg_flag) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		pr_debug("\tVIRTIO_MMIO_MAGIC_VALUE\n");
		break;
	case VIRTIO_MMIO_VERSION:
		pr_debug("\tVIRTIO_MMIO_VERSION\n");
		break;
	case VIRTIO_MMIO_DEVICE_ID:
		pr_debug("\tVIRTIO_MMIO_DEVICE_ID\n");
		break;
	case VIRTIO_MMIO_VENDOR_ID:
		pr_debug("\tVIRTIO_MMIO_VENDOR_ID\n");
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		pr_debug("\tVIRTIO_MMIO_DEVICE_FEATURES\n");
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		pr_debug("\tVIRTIO_MMIO_DEVICE_FEATURES_SEL\n");
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		pr_debug("\tVIRTIO_MMIO_DRIVER_FEATURES\n");
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		pr_debug("\tVIRTIO_MMIO_DRIVER_FEATURES_SEL\n");
		break;
	case VIRTIO_MMIO_GUEST_PAGE_SIZE:
		pr_debug("\tVIRTIO_MMIO_GUEST_PAGE_SIZE\n");
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		pr_debug("\tVIRTIO_MMIO_QUEUE_SEL\n");
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		pr_debug("\tVIRTIO_MMIO_QUEUE_NUM_MAX\n");
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		pr_debug("\tVIRTIO_MMIO_QUEUE_NUM\n");
		break;
	case VIRTIO_MMIO_QUEUE_ALIGN:
		pr_debug("\tVIRTIO_MMIO_QUEUE_ALIGN\n");
		break;
	case VIRTIO_MMIO_QUEUE_PFN:
		pr_debug("\tVIRTIO_MMIO_QUEUE_PFN\n");
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		pr_debug("\tVIRTIO_MMIO_QUEUE_READY\n");
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		pr_debug("\tVIRTIO_MMIO_QUEUE_NOTIFY\n");
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		pr_debug("\tVIRTIO_MMIO_INTERRUPT_STATUS\n");
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		pr_debug("\tVIRTIO_MMIO_INTERRUPT_ACK\n");
		break;
	case VIRTIO_MMIO_STATUS:
		pr_debug("\tVIRTIO_MMIO_STATUS\n");
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		pr_debug("\tVIRTIO_MMIO_QUEUE_DESC_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		pr_debug("\tVIRTIO_MMIO_QUEUE_DESC_HIGH\n");
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		pr_debug("\tVIRTIO_MMIO_QUEUE_AVAIL_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		pr_debug("\tVIRTIO_MMIO_QUEUE_AVAIL_HIGH\n");
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		pr_debug("\tVIRTIO_MMIO_QUEUE_USED_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		pr_debug("\tVIRTIO_MMIO_QUEUE_USED_HIGH\n");
		break;
	case VIRTIO_MMIO_SHM_SEL:
		pr_debug("\tVIRTIO_MMIO_SHM_SEL\n");
		break;
	case VIRTIO_MMIO_SHM_LEN_LOW:
		pr_debug("\tVIRTIO_MMIO_SHM_LEN_LOW\n");
		break;
	case VIRTIO_MMIO_SHM_LEN_HIGH:
		pr_debug("\tVIRTIO_MMIO_SHM_LEN_HIGH\n");
		break;
	case VIRTIO_MMIO_SHM_BASE_LOW:
		pr_debug("\tVIRTIO_MMIO_SHM_BASE_LOW\n");
		break;
	case VIRTIO_MMIO_SHM_BASE_HIGH:
		pr_debug("\tVIRTIO_MMIO_SHM_BASE_HIGH\n");
		break;
	case VIRTIO_MMIO_CONFIG_GENERATION:
		pr_debug("\tVIRTIO_MMIO_CONFIG_GENERATION\n");
		break;
	default:
		if (neg_flag >= VIRTIO_MMIO_CONFIG)
			pr_debug("\tVIRTIO_MMIO_CONFIG\n");
		else
			pr_debug("\tNegotiation flag Unknown: %lld\n",
				 neg_flag);
		return;
	}
}

/*
 * Print the pdev:
 *
 *static void print_virtio_pdev(struct platform_device *pdev)
 *{
 *	int i;
 *
 *	pr_info("Print the pdev:\n");
 *	pr_info("\t.name = %s\n", pdev->name);
 *	pr_info("\t.id = %d\n", pdev->id);
 *	pr_info("\t.num_resources = %d\n", pdev->num_resources);
 *
 *	for (i=0; i < pdev->num_resources; i++) {
 *		pr_info("\t.num_resource = %d\n", i);
 *		pr_info("\t\t.start = 0x%llx\n", pdev->resource[i].start);
 *		pr_info("\t\t.end = 0x%llx\n", pdev->resource[i].end);
 *		pr_info("\t\t.flags = 0x%lx\n", pdev->resource[i].flags);
 *	}
 *}
 *
 *Result:
 *
 *	.name = a003e00.virtio_loopback
 *	.id = -1
 *	.num_resources = 2
 *	.num_resource = 0
 *		.start = 0xa003e00
 *		.end = 0xa003fff
 *		.flags = 0x200
 *	.num_resource = 1
 *		.start = 0x2c
 *		.end = 0x2c
 *		.flags = 0x401
 */

/* function declaration */
static uint64_t read_adapter(uint64_t fn_id, uint64_t size,
			     struct device_data *dev_data);
static void write_adapter(uint64_t data, uint64_t fn_id, uint64_t size,
			  struct device_data *dev_data);

/* Configuration interface */
static u64 vl_get_features(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;
	u64 features;

	/* Take feature bits 0-31 */
	write_adapter(1, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 4, data);
	features = read_adapter(VIRTIO_MMIO_DEVICE_FEATURES, 4, data);
	features <<= 32;

	/* Take feature bits 32-63 */
	write_adapter(0, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 4, data);
	features |= read_adapter(VIRTIO_MMIO_DEVICE_FEATURES, 4, data);

	return features;
}

static int vl_finalize_features(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Make sure there are no mixed devices */
	if (vl_dev->version == 2 &&
	    !__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev,
			"New virtio-mmio devices (version 2) must provide VIRTIO_F_VERSION_1 feature!\n");
		return -EINVAL;
	}

	write_adapter(1, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 4, data);
	write_adapter((u32)(vdev->features >> 32), VIRTIO_MMIO_DRIVER_FEATURES,
		      4, data);

	write_adapter(0, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 4, data);
	write_adapter((u32)vdev->features, VIRTIO_MMIO_DRIVER_FEATURES,
		      4, data);

	return 0;
}

static void vl_get(struct virtio_device *vdev, unsigned int offset,
				   void *buf, unsigned int len)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	u8 b;
	__le16 w;
	__le32 l;

	if (vl_dev->version == 1) {
		u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			ptr[i] = read_adapter(VIRTIO_MMIO_CONFIG + offset + i,
					      1, data);
		return;
	}

	switch (len) {
	case 1:
		b = read_adapter(VIRTIO_MMIO_CONFIG + offset, 1, data);
		memcpy(buf, &b, sizeof(b));
		break;
	case 2:
		w = cpu_to_le16(read_adapter(VIRTIO_MMIO_CONFIG + offset,
					     2, data));
		memcpy(buf, &w, sizeof(w));
		break;
	case 4:
		l = cpu_to_le32(read_adapter(VIRTIO_MMIO_CONFIG + offset,
					     4, data));
		memcpy(buf, &l, sizeof(l));
		break;
	case 8:
		l = cpu_to_le32(read_adapter(VIRTIO_MMIO_CONFIG + offset,
					     4, data));
		memcpy(buf, &l, sizeof(l));
		l = cpu_to_le32(read_adapter(
					VIRTIO_MMIO_CONFIG + offset + sizeof(l),
					4, data));
		memcpy(buf + sizeof(l), &l, sizeof(l));
		break;
	default:
		BUG();
	}
}

static void vl_set(struct virtio_device *vdev, unsigned int offset,
					const void *buf, unsigned int len)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	u8 b;
	__le16 w;
	__le32 l;

	if (vl_dev->version == 1) {
		const u8 *ptr = buf;
		int i;

		for (i = 0; i < len; i++)
			write_adapter(ptr[i], VIRTIO_MMIO_CONFIG + offset + i,
				      1, data);

		return;
	}

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof(b));
		write_adapter(b, VIRTIO_MMIO_CONFIG + offset, 1, data);
		break;
	case 2:
		memcpy(&w, buf, sizeof(w));
		write_adapter(le16_to_cpu(w), VIRTIO_MMIO_CONFIG + offset,
			      2, data);
		break;
	case 4:
		memcpy(&l, buf, sizeof(l));
		write_adapter(le32_to_cpu(l), VIRTIO_MMIO_CONFIG + offset,
			      4, data);
		break;
	case 8:
		memcpy(&l, buf, sizeof(l));
		write_adapter(le32_to_cpu(l), VIRTIO_MMIO_CONFIG + offset,
			      4, data);
		memcpy(&l, buf + sizeof(l), sizeof(l));
		write_adapter(le32_to_cpu(l),
			      VIRTIO_MMIO_CONFIG + offset + sizeof(l),
			      4, data);
		break;
	default:
		BUG();
	}
}

static u32 vl_generation(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	if (vl_dev->version == 1)
		return 0;
	else
		return read_adapter(VIRTIO_MMIO_CONFIG_GENERATION, 4, data);
}

static u8 vl_get_status(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	return read_adapter(VIRTIO_MMIO_STATUS, 4, data) & 0xff;
}

static void vl_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	write_adapter(status, VIRTIO_MMIO_STATUS, 4, data);
}

static void vl_reset(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;

	/* 0 status means a reset. */
	write_adapter(0, VIRTIO_MMIO_STATUS, 4, data);
}

/* Notify work handling function */
static void notify_work_handler(struct work_struct *work)
{
	struct virtio_loopback_device *vl_dev =
		container_of(work, struct virtio_loopback_device, notify_work);
	struct device_data *dev_data = vl_dev->data;
	struct notify_data *entry, *tmp;
	uint32_t index;

	spin_lock(&vl_dev->notify_q_lock);
	list_for_each_entry_safe(entry, tmp, &vl_dev->notify_list, list) {
		index = entry->index;
		list_del(&entry->list);
		kfree(entry);
		/* Proceed in dispatching the notification to the adapter */
		spin_unlock(&vl_dev->notify_q_lock);
		write_adapter(index, VIRTIO_MMIO_QUEUE_NOTIFY, 4, dev_data);
		spin_lock(&vl_dev->notify_q_lock);
	}
	spin_unlock(&vl_dev->notify_q_lock);
}

/* The notify function used when creating a virtqueue */
static bool vl_notify(struct virtqueue *vq)
{
	struct virtio_loopback_device *vl_dev =
		to_virtio_loopback_device(vq->vdev);
	struct eventfd_ctx **vq_notifiers = vl_dev->data->vq_data.vq_notifiers;
	bool vq_notifiers_enabled = vl_dev->data->vq_data.vq_notifiers_enabled;
	struct notify_data *data;
	int ret = 1;

	if (vq_notifiers_enabled && (vq_notifiers[vq->index])) {
		/* Notify directly vhost-user-device bypassing the adapter */
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 7, 12)
		eventfd_signal(vq_notifiers[vq->index]);
#else
		eventfd_signal(vq_notifiers[vq->index], 1);
#endif
	} else {
		/* Create the new node */
		data = kmalloc(sizeof(struct notify_data), GFP_ATOMIC);
		if (!data)
			return false;

		data->index = vq->index;
		INIT_LIST_HEAD(&data->list);

		/* Add in the notify_list, which should be protected! */
		spin_lock(&vl_dev->notify_q_lock);
		list_add_tail(&data->list, &vl_dev->notify_list);
		spin_unlock(&vl_dev->notify_q_lock);

		/* Schedule the element */
		while (ret) {
			/*
			 * Force scheduling if queue_work fails and
			 * list is not empty
			 */
			ret = !queue_work(vl_dev->notify_workqueue,
					  &vl_dev->notify_work);
			spin_lock(&vl_dev->notify_q_lock);
			ret &= !list_empty(&vl_dev->notify_list);
			spin_unlock(&vl_dev->notify_q_lock);
		}
	}

	return true;
}

/* the interrupt function used when receiving an IRQ */
bool vl_interrupt(struct virtio_loopback_device *vl_dev, int irq)
{
	struct device_data *data = vl_dev->data;
	struct virtio_loopback_vq_info *info;
	unsigned long status;

	/*
	 * Read and acknowledge interrupts
	 *
	 * Those two operations should be executed without any
	 * intermediate status change.
	 */
	status = read_adapter(VIRTIO_MMIO_INTERRUPT_STATUS, 4, data);
	write_adapter(status, VIRTIO_MMIO_INTERRUPT_ACK, 4, data);

	if (unlikely(status & VIRTIO_MMIO_INT_CONFIG))
		virtio_config_changed(&vl_dev->vdev);

	if (likely(status & VIRTIO_MMIO_INT_VRING)) {
		spin_lock(&vl_dev->lock);
		list_for_each_entry(info, &vl_dev->virtqueues, node) {
			(void)vring_interrupt(irq, info->vq);
		}
		spin_unlock(&vl_dev->lock);
	}

	return true;
}

static void vl_del_vq(struct virtqueue *vq)
{
	struct virtio_loopback_device *vl_dev =
		to_virtio_loopback_device(vq->vdev);
	struct device_data *data = vl_dev->data;

	struct virtio_loopback_vq_info *info = vq->priv;
	unsigned long flags;
	unsigned int index = vq->index;

	spin_lock_irqsave(&vl_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vl_dev->lock, flags);

	/* Select and deactivate the queue */
	write_adapter(index, VIRTIO_MMIO_QUEUE_SEL, 4, data);

	if (vl_dev->version == 1) {
		write_adapter(0, VIRTIO_MMIO_QUEUE_PFN, 4, data);
	} else {
		write_adapter(0, VIRTIO_MMIO_QUEUE_READY, 4, data);
		WARN_ON(read_adapter(VIRTIO_MMIO_QUEUE_READY, 4, data));
	}

	vring_del_virtqueue(vq);
	kfree(info);
}

static void vl_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list)
		vl_del_vq(vq);
}

static struct virtqueue *vl_setup_vq(struct virtio_device *vdev,
				     unsigned int index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name, bool ctx)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;
	struct virtio_loopback_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags;
	unsigned int num;
	int err;

	if (!name)
		return NULL;

	/* Select the queue we're interested in */
	write_adapter(index, VIRTIO_MMIO_QUEUE_SEL, 4, data);

	/* Queue shouldn't already be set up. */
	if (read_adapter((vl_dev->version == 1 ?
			 VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY),
			 4, data)) {
		err = -ENOENT;
		goto error_available;
	}

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto error_kmalloc;
	}

	num = read_adapter(VIRTIO_MMIO_QUEUE_NUM_MAX, 4, data);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}

	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				    true, true, ctx, vl_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
	vq->num_max = num;
#endif

	/* Activate the queue */
	write_adapter(virtqueue_get_vring_size(vq), VIRTIO_MMIO_QUEUE_NUM, 4,
		      data);
	if (vl_dev->version == 1) {
		u64 q_pfn = virtqueue_get_desc_addr(vq);

		q_pfn = q_pfn >> PAGE_SHIFT;

		/* Copy the physical address and enable the mmap */
		data->vq_data.vq_pfn = q_pfn;
		data->vq_data.vq_pfns[data->vq_data.vq_index++] = q_pfn;

		/*
		 * virtio-loopback v1 uses a 32bit QUEUE PFN. If we have
		 * something that doesn't fit in 32bit, fail the setup rather
		 * than pretending to be successful.
		 */
		if (q_pfn >> 32) {
			dev_err(&vdev->dev,
				"platform bug: legacy virtio-loopback must not be used with RAM above 0x%llxGB\n",
				0x1ULL << (32 + PAGE_SHIFT - 30));
			err = -E2BIG;
			goto error_bad_pfn;
		}

		write_adapter(PAGE_SIZE, VIRTIO_MMIO_QUEUE_ALIGN, 4, data);
		write_adapter(q_pfn, VIRTIO_MMIO_QUEUE_PFN, 4, data);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_DESC_LOW, 4, data);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_DESC_HIGH,
			      4, data);

		addr = virtqueue_get_avail_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_AVAIL_LOW, 4, data);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
			      4, data);

		addr = virtqueue_get_used_addr(vq);
		write_adapter((u32)addr, VIRTIO_MMIO_QUEUE_USED_LOW, 4, data);
		write_adapter((u32)(addr >> 32), VIRTIO_MMIO_QUEUE_USED_HIGH,
			      4, data);

		write_adapter(1, VIRTIO_MMIO_QUEUE_READY, 4, data);
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vl_dev->lock, flags);
	list_add(&info->node, &vl_dev->virtqueues);
	spin_unlock_irqrestore(&vl_dev->lock, flags);

	return vq;

error_bad_pfn:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	if (vl_dev->version == 1) {
		write_adapter(0, VIRTIO_MMIO_QUEUE_PFN, 4, data);
	} else {
		write_adapter(0, VIRTIO_MMIO_QUEUE_READY, 4, data);
		WARN_ON(read_adapter(VIRTIO_MMIO_QUEUE_READY, 4, data));
	}
	kfree(info);
error_kmalloc:
error_available:
	return ERR_PTR(err);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 10, 8)
static int vl_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
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

		vqs[i] = vl_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
							 ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			vl_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}
#else
static int vl_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			   struct virtqueue *vqs[],
			   struct virtqueue_info vqs_info[],
			   struct irq_affinity *desc)
{
	int i, queue_idx = 0;

	for (i = 0; i < nvqs; ++i) {
		struct virtqueue_info *vqi = &vqs_info[i];

		if (!vqi->name) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = vl_setup_vq(vdev, queue_idx++, vqi->callback,
				     vqi->name, vqi->ctx);
		if (IS_ERR(vqs[i])) {
			vl_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}

	return 0;
}
#endif

static const char *vl_bus_name(struct virtio_device *vdev)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);

	return vl_dev->pdev->name;
}

static bool vl_get_shm_region(struct virtio_device *vdev,
			      struct virtio_shm_region *region, u8 id)
{
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct device_data *data = vl_dev->data;
	u64 len, addr;

	/* Select the region we're interested in */
	write_adapter(id, VIRTIO_MMIO_SHM_SEL, 4, data);

	/* Read the region size */
	len = (u64) read_adapter(VIRTIO_MMIO_SHM_LEN_LOW, 4, data);
	len |= (u64) read_adapter(VIRTIO_MMIO_SHM_LEN_HIGH, 4, data) << 32;

	region->len = len;

	/* Check if region length is -1. If that's the case, the shared memory
	 * region does not exist and there is no need to proceed further.
	 */
	if (len == ~(u64)0)
		return false;

	/* Read the region base address */
	addr = (u64) read_adapter(VIRTIO_MMIO_SHM_BASE_LOW, 4, data);
	addr |= (u64) read_adapter(VIRTIO_MMIO_SHM_BASE_HIGH, 4, data) << 32;

	region->addr = addr;

	return true;
}

static const struct virtio_config_ops virtio_loopback_config_ops = {
	.get			= vl_get,
	.set			= vl_set,
	.generation		= vl_generation,
	.get_status		= vl_get_status,
	.set_status		= vl_set_status,
	.reset			= vl_reset,
	.find_vqs		= vl_find_vqs,
	.del_vqs		= vl_del_vqs,
	.get_features		= vl_get_features,
	.finalize_features	= vl_finalize_features,
	.bus_name		= vl_bus_name,
	.get_shm_region		= vl_get_shm_region,
};

static void virtio_loopback_release_dev(struct device *_d)
{
	struct virtio_device *vdev =
		container_of(_d, struct virtio_device, dev);
	struct virtio_loopback_device *vl_dev = to_virtio_loopback_device(vdev);
	struct platform_device *pdev = vl_dev->pdev;

	devm_kfree(&pdev->dev, vl_dev);
}

/* Function to carry-out the registration of the virtio_loopback */
int loopback_register_virtio_dev(struct virtio_loopback_device *vl_dev)
{
	struct platform_device *pdev = vl_dev->pdev;
	struct device_data *data = vl_dev->data;
	unsigned long magic;
	int rc;

	/* Check magic value */
	magic = read_adapter(VIRTIO_MMIO_MAGIC_VALUE, 4, data);

	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}

	/* Check device version */
	vl_dev->version = read_adapter(VIRTIO_MMIO_VERSION, 4, data);

	if (vl_dev->version < 1 || vl_dev->version > 2) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
							vl_dev->version);
		return -ENXIO;
	}

	vl_dev->vdev.id.device = read_adapter(VIRTIO_MMIO_DEVICE_ID, 4, data);

	if (vl_dev->vdev.id.device == 0) {
		/*
		 * virtio-loopback device with an ID 0 is a (dummy) placeholder
		 * with no function. End probing now with no error reported.
		 */
		return -ENODEV;
	}

	vl_dev->vdev.id.vendor = read_adapter(VIRTIO_MMIO_VENDOR_ID, 4, data);

	if (vl_dev->version == 1) {
		write_adapter(PAGE_SIZE, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4, data);

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
		dev_warn(&pdev->dev,
			 "Failed to enable 64-bit or 32-bit DMA. Trying to continue, but this might not work.\n");

	/* Register the virtio device in the system */
	rc = register_virtio_device(&vl_dev->vdev);
	if (rc)
		put_device(&vl_dev->vdev.dev);

	return 0;
}

static int virtio_loopback_probe(struct platform_device *pdev)
{
	int err;
	struct virtio_loopback_device *vl_dev;

	pr_info("Entered probe with id: %d!\n", pdev->id);
	vl_dev = devm_kzalloc(&pdev->dev, sizeof(*vl_dev), GFP_KERNEL);
	if (!vl_dev) {
		err = -ENOMEM;
		goto out;
	}

	vl_dev->vdev.dev.parent = &pdev->dev;
	vl_dev->vdev.dev.release = virtio_loopback_release_dev;
	vl_dev->vdev.config = &virtio_loopback_config_ops;
	vl_dev->pdev = pdev;
	INIT_LIST_HEAD(&vl_dev->virtqueues);
	spin_lock_init(&vl_dev->lock);
	/* Initialize the workqueue */
	vl_dev->notify_workqueue =
		create_singlethread_workqueue("notify_workqueue");
	INIT_WORK(&vl_dev->notify_work, notify_work_handler);
	INIT_LIST_HEAD(&vl_dev->notify_list);
	spin_lock_init(&vl_dev->notify_q_lock);

	platform_set_drvdata(pdev, vl_dev);

	/* Insert new entry data */
	err = insert_entry_data(vl_dev, pdev->id);

out:
	return err;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 10, 8)
void virtio_loopback_remove(struct platform_device *pdev)
#else
int virtio_loopback_remove(struct platform_device *pdev)
#endif
{
	struct virtio_loopback_device *vl_dev = platform_get_drvdata(pdev);

	/* Destroy the notify workqueue */
	flush_workqueue(vl_dev->notify_workqueue);
	destroy_workqueue(vl_dev->notify_workqueue);

	if (vl_dev->data) {
		unregister_virtio_device(&vl_dev->vdev);
		pr_info("unregister_virtio_device!\n");
		/* Proceed to de-activating the data for this entry */
		vl_dev->data = NULL;
	}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 10, 8)
	return 0;
#endif
}

/* No need of DTS and ACPI */
struct platform_driver virtio_loopback_driver = {
	.probe	 = virtio_loopback_probe,
	.remove	 = virtio_loopback_remove,
	.driver	 = {
		.name   = "loopback-transport",
	},
};

static uint64_t read_adapter(uint64_t fn_id, uint64_t size,
			     struct device_data *dev_data)
{
	uint64_t result;

	mutex_lock(&(dev_data)->read_write_lock);

	/*
	 * By enabling the following line all
	 * read messages will be printed:
	 *
	 * print_neg_flag(fn_id, 1);
	 */
	print_neg_flag(fn_id, 1);

	((struct virtio_neg *)(dev_data->info->data))->notification = fn_id;
	((struct virtio_neg *)(dev_data->info->data))->data = 0;
	((struct virtio_neg *)(dev_data->info->data))->size = size;
	((struct virtio_neg *)(dev_data->info->data))->read = true;

	atomic_set(&((struct virtio_neg *)(dev_data->info->data))->done, 0);

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 7, 12)
	eventfd_signal(dev_data->efd_ctx);
#else
	eventfd_signal(dev_data->efd_ctx, 1);
#endif

	/*
	 * There is a chance virtio-loopback adapter to call "wake_up"
	 * before the current thread sleep. This is the reason that
	 * "wait_event_timeout" is used instead of "wait_event". In this
	 * way, virtio-loopback driver will wake up even if has missed the
	 * "wake_up" kick, check the updated "done" value and return.
	 */

	while (dev_data->valid_eventfd &&
	       atomic_read(&((struct virtio_neg *)(dev_data->info->data))->done) != 1)
		wait_event_timeout(dev_data->wq,
			atomic_read(&((struct virtio_neg *)(dev_data->info->data))->done) == 1,
			1 * HZ);

	result = ((struct virtio_neg *)(dev_data->info->data))->data;

	mutex_unlock(&(dev_data)->read_write_lock);

	return result;
}

static void write_adapter(uint64_t data, uint64_t fn_id, uint64_t size,
			  struct device_data *dev_data)
{

	mutex_lock(&(dev_data)->read_write_lock);

	/*
	 * By enabling the following line all
	 * write messages will be printed:
	 *
	 * print_neg_flag(fn_id, 1);
	 */
	print_neg_flag(fn_id, 0);

	((struct virtio_neg *)(dev_data->info->data))->notification = fn_id;
	((struct virtio_neg *)(dev_data->info->data))->data = data;
	((struct virtio_neg *)(dev_data->info->data))->size = size;
	((struct virtio_neg *)(dev_data->info->data))->read = false;

	atomic_set(&((struct virtio_neg *)(dev_data->info->data))->done, 0);

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 7, 12)
	eventfd_signal(dev_data->efd_ctx);
#else
	eventfd_signal(dev_data->efd_ctx, 1);
#endif

	/*
	 * There is a chance virtio-loopback adapter to call "wake_up"
	 * before the current thread sleep. This is the reason that
	 * "wait_event_timeout" is used instead of "wait_event". In this
	 * way, virtio-loopback driver will wake up even if has missed the
	 * "wake_up" kick, check the updated "done" value and return.
	 */
	while (dev_data->valid_eventfd &&
	       atomic_read(&((struct virtio_neg *)(dev_data->info->data))->done) != 1)
		wait_event_timeout(dev_data->wq,
			atomic_read(&((struct virtio_neg *)(dev_data->info->data))->done) == 1,
			1 * HZ);

	mutex_unlock(&(dev_data)->read_write_lock);
}
