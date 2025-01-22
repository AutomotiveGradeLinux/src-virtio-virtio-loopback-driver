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

	/* Add VIRTIO_F_ACCESS_PLATFORM */
	features |= (u64)(1) << VIRTIO_F_ACCESS_PLATFORM;

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

static bool trigger_notification(struct virtqueue *vq)
{
	struct virtio_loopback_device *vl_dev =
		to_virtio_loopback_device(vq->vdev);
	struct eventfd_ctx **vq_notifiers = vl_dev->data->vq_data.vq_notifiers;
	bool vq_notifiers_enabled = vl_dev->data->vq_data.vq_notifiers_enabled;
	int ret;

	if (vq_notifiers_enabled && (vq_notifiers[vq->index])) {
		/* Notify directly vhost-user-device bypassing the adapter */
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 7, 12)
		eventfd_signal(vq_notifiers[vq->index]);
#else
		eventfd_signal(vq_notifiers[vq->index], 1);
#endif
	} else {
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

/* Notify work handling function */
static void trigger_dev_notif(struct virtio_loopback_device *vl_dev)
{
	struct notify_data *entry, *tmp;
	uint32_t index;
	struct virtio_loopback_vq_info *info;

	if (atomic_read(&vl_dev->data->avail_notifs) == 0)
		return;

	spin_lock(&vl_dev->notify_q_lock);
	list_for_each_entry_safe(entry, tmp, &vl_dev->notify_list, list) {
		index = entry->index;
		list_del(&entry->list);
		kfree(entry);
		spin_unlock(&vl_dev->notify_q_lock);

		/* Decrease atomically the notification counters */
		atomic_dec(&vl_dev->data->avail_notifs);
		atomic_dec(&loopback_devices.pending_notifs);

		/* Find which is the corresponing vq and trigger the notification */
		list_for_each_entry(info, &vl_dev->virtqueues, node) {
			if (info->vq->index == index) {
				(void)trigger_notification(info->vq);
				/* Decrease the notification handlers */
				return;
			}
		}
		spin_lock(&vl_dev->notify_q_lock);
	}
	spin_unlock(&vl_dev->notify_q_lock);
}

static bool available_notifications(void)
{
	return atomic_read(&loopback_devices.pending_notifs) > 0;
}

static void set_dev_credits(struct virtio_loopback_device *vl_dev, int64_t remaining_credits)
{
	if (remaining_credits > 0) {
		if (remaining_credits > vl_dev->data->vdev_data->init_notif_credits)
			atomic_set(&vl_dev->data->notif_credits, vl_dev->data->vdev_data->init_notif_credits);
		else
			atomic_set(&vl_dev->data->notif_credits, (uint32_t)remaining_credits);
	} else {
		atomic_set(&vl_dev->data->notif_credits, 0);
	}
}

static void reset_credits(struct virtio_loopback_device *vl_dev)
{
	/* Update timestamp & available credits */
	vl_dev->data->served_timestamp = ktime_get();
	set_dev_credits(vl_dev, vl_dev->data->vdev_data->init_notif_credits);
}

static uint32_t read_dev_credits(struct virtio_loopback_device *vl_dev)
{
	return atomic_read(&vl_dev->data->notif_credits);
}

static uint32_t read_dev_notifs(struct virtio_loopback_device *vl_dev)
{
	return atomic_read(&vl_dev->data->avail_notifs);
}

static struct virtio_loopback_device_node *head_elem(void)
{
	struct virtio_loopback_device_node *device;

	spin_lock(&loopback_devices.running_lock);
	device = list_first_entry_or_null(
			&loopback_devices.virtio_devices_list,
			struct virtio_loopback_device_node,
			node);
	spin_unlock(&loopback_devices.running_lock);
	return device;
}

static struct virtio_loopback_device_node *
 next_elem(struct virtio_loopback_device_node *device)
{
	int ret;

	device = list_next_entry(device, node);

	/* If reached the list head, wrap around to the beginning */
	spin_lock(&loopback_devices.running_lock);
	ret = list_entry_is_head(device, &loopback_devices.virtio_devices_list, node);
	spin_unlock(&loopback_devices.running_lock);

	if (ret)
		device = head_elem();

	return device;
}

bool add_dev_to_list(uint32_t array_dev_pos)
{
	struct virtio_loopback_device_node *dev_node;

	/* Add this device to a global list */
	dev_node = kmalloc(sizeof(struct virtio_loopback_device_node), GFP_ATOMIC);
	if (!dev_node)
		return false;

	/* TODO: Check the next line */
	dev_node->vq_index = array_dev_pos;
	INIT_LIST_HEAD(&dev_node->node);
	atomic_set(&dev_node->is_deleted, 0);

	spin_lock(&loopback_devices.running_lock);
	list_add_tail(&dev_node->node, &loopback_devices.virtio_devices_list);
	spin_unlock(&loopback_devices.running_lock);

	return true;
}

static bool is_dev_deleted(struct virtio_loopback_device_node *device)
{
	return atomic_read(&device->is_deleted) == 1;
}

static void del_dev_from_list(struct virtio_loopback_device_node *device)
{
	spin_lock(&loopback_devices.running_lock);
	list_del(&device->node);
	spin_unlock(&loopback_devices.running_lock);
	kfree(device);
}

/*
 * void clean_dev_notifs_inters(struct virtio_loopback_device_node *device)
 * {
 * 	struct notify_data *entry, *tmp;
 * 	struct virtio_loopback_device *vl_dev = loopback_devices.devices[device->vq_index];
 * 	int i, avail_inters = atomic_read(&vl_dev->data->avail_inters);
 *
 * 	spin_lock(&vl_dev->notify_q_lock);
 * 	list_for_each_entry_safe(entry, tmp, &vl_dev->notify_list, list) {
 * 		atomic_dec(&vl_dev->data->avail_notifs);
 * 		atomic_dec(&loopback_devices.pending_notifs);
 * 	}
 * 	spin_unlock(&vl_dev->notify_q_lock);
 *
 * 	for (i = 0; i < avail_inters; i++) {
 * 		atomic_dec(&vl_dev->data->avail_inters);
 * 		atomic_dec(&loopback_devices.pending_inters);
 * 	}
 * }
 */

void note_dev_deletion(struct virtio_loopback_device *vl_dev)
{
	struct virtio_loopback_device_node *device, *temp = NULL;

	spin_lock(&loopback_devices.running_lock);
	list_for_each_entry(device, &loopback_devices.virtio_devices_list, node) {
		if (vl_dev == loopback_devices.devices[device->vq_index]) {
			temp = device;
			break;
		}
	}
	spin_unlock(&loopback_devices.running_lock);

	if (temp)
		atomic_set(&device->is_deleted, 1);
}

/*
 * void clean_deleted_devs(void)
 * {
 * 	struct virtio_loopback_device_node *temp = NULL;
 *
 * 	spin_lock(&loopback_devices.running_lock);
 * 	list_for_each_entry_safe(device, temp, &loopback_devices.virtio_devices_list, node) {
 * 		if (is_dev_deleted(device)) {
 * 			list_del(&device->node);
 * 			kfree(device);
 * 		}
 * 	}
 * 	spin_unlock(&loopback_devices.running_lock);
 * }
 */

static void clean_all_devs(void)
{
	struct virtio_loopback_device_node *device = NULL, *temp = NULL;

	spin_lock(&loopback_devices.running_lock);
	list_for_each_entry_safe(device, temp, &loopback_devices.virtio_devices_list, node) {
		list_del(&device->node);
		kfree(device);
	}
	spin_unlock(&loopback_devices.running_lock);
}

/*
 * static bool is_node_in_list(struct virtio_loopback_device_node *device)
 * {
 * 	struct virtio_loopback_device_node *temp;
 * 	bool ret = false;
 *
 * 	rcu_read_lock();
 * 	list_for_each_entry_rcu(temp, &loopback_devices.virtio_devices_list, node) {
 * 		if (temp == device) {
 * 			ret = true;
 * 			break;
 * 		}
 * 	}
 * 	rcu_read_unlock();
 *
 * 	return ret;
 * }
 */

static bool available_interrupts(void)
{
	return atomic_read(&loopback_devices.pending_inters) > 0;
}

static uint32_t read_dev_inters(struct virtio_loopback_device *vl_dev)
{
	return atomic_read(&vl_dev->data->avail_inters);
}

static uint32_t highest_active_priority_notifs(void)
{
	struct virtio_loopback_device_node *device;
	struct virtio_loopback_device *vl_dev;
	uint32_t max_priority = 0;

	spin_lock(&loopback_devices.running_lock);
	list_for_each_entry(device, &loopback_devices.virtio_devices_list, node) {
		if (is_dev_deleted(device))
			continue;
		vl_dev = loopback_devices.devices[device->vq_index];
		if ((read_dev_notifs(vl_dev) > 0) || (read_dev_inters(vl_dev) > 0))
			if (vl_dev->data->priority_group > max_priority)
				max_priority = vl_dev->data->priority_group;
	}
	spin_unlock(&loopback_devices.running_lock);

	return max_priority;
}

static void update_highest_active_prior_notifs(void)
{
	uint32_t current_highest_priority = highest_active_priority_notifs();

	atomic_set(&loopback_devices.highest_active_prior_notifs, current_highest_priority);
}

static bool dev_highest_prior_notifs(struct virtio_loopback_device *vl_dev)
{
	return vl_dev->data->priority_group >=
		atomic_read(&loopback_devices.highest_active_prior_notifs);
}

static uint64_t read_dev_served_timestamp(struct virtio_loopback_device *vl_dev)
{
	return vl_dev->data->served_timestamp;
}

static bool oldest_active_dev_in_group(struct virtio_loopback_device *curr_vl_dev)
{
	struct virtio_loopback_device_node *device;
	struct virtio_loopback_device *vl_dev;
	uint64_t oldest_active_dev_time = (uint64_t)ktime_get();

	spin_lock(&loopback_devices.running_lock);
	list_for_each_entry(device, &loopback_devices.virtio_devices_list, node) {
		if (is_dev_deleted(device))
			continue;
		vl_dev = loopback_devices.devices[device->vq_index];
		/* Iterate only on active devices */
		if ((read_dev_notifs(vl_dev) > 0) || (read_dev_inters(vl_dev) > 0))
			/* Iterate only on active devices the same group */
			if ((vl_dev->data->priority_group == curr_vl_dev->data->priority_group) &&
				(read_dev_served_timestamp(vl_dev) < oldest_active_dev_time))
				/* Save the oldest timestamp of a device aligned with above critirias */
				oldest_active_dev_time = read_dev_served_timestamp(vl_dev);
	}
	spin_unlock(&loopback_devices.running_lock);

	return oldest_active_dev_time == read_dev_served_timestamp(curr_vl_dev);
}

/* the interrupt function used when receiving an IRQ */
static bool vl_interrupt(struct virtio_loopback_device *vl_dev, int irq)
{
	struct virtio_loopback_vq_info *info;

	pr_debug("vl_interrupt\n");
	spin_lock(&vl_dev->lock);
	list_for_each_entry(info, &vl_dev->virtqueues, node) {
		(void)vring_interrupt(irq, info->vq);
	}
	spin_unlock(&vl_dev->lock);

	return true;
}

/*
 * Pseudo algorith: with groups (implementation 1)
 *
 * For dev in dev_list
 *
 *     if dev->priority != active_list_highest_prior or
 *        dev_idle or
 *        dev_older_in_group()
 *         go next
 *
 *     while(time(dev_credits) {
 *             trigger_notifications
 *     }
 *
 *    update_highest_priority()
 *
 */

/*
 * Pseudo algorith: with groups (implementation 2)
 *
 * idle_list_dev = dev_1, dev_2, ... , dev_n
 * active_list_dev = null
 * active_list_highest_prior = 'A'
 *
 * for dev in active_list_dev
 *
 *     if dev->priority != active_list_highest_prior or
 *        dev_older_in_group()
 *         go next
 *
 *     while(time(cred_dev))
 *         trigger_notifications
 *
 *     remove(dev, active_list_dev)
 *     add(dev, idle_list_dev)
 *     update_highest_priority()
 *
 */

int notif_sched_func(void *data)
{
	struct virtio_loopback_device *vl_dev;
	struct virtio_loopback_device_node *device = NULL, *temp = NULL;
	ktime_t starting_time, deadline;

	/* Wait the first notification */
	while (!available_notifications() && !kthread_should_stop()) {
		wait_event_timeout(
			loopback_devices.wq_notifs_inters,
			available_notifications() || kthread_should_stop(),
			100 * HZ);
	}

	if (kthread_should_stop())
		goto sched_exit;

	device = head_elem();
	if (unlikely(!device)) {
		pr_err("Device list is empty - exit\n");
		return 1;
	}

	while (!kthread_should_stop()) {
		if ((available_notifications() ||
		    available_interrupts()) &&
		    !list_empty(&loopback_devices.virtio_devices_list)) {

			if (is_dev_deleted(device)) {
				temp = device;
				device = next_elem(device);
				del_dev_from_list(temp);
				continue;
			}

			vl_dev = loopback_devices.devices[device->vq_index];

			pr_debug("Available notifs: %u\n", atomic_read(&loopback_devices.pending_notifs));
			pr_debug("Available inters: %u\n", atomic_read(&loopback_devices.pending_inters));
			pr_debug("Device %lu avail credits: %u, avail notifications %u, avail_inters: %u\n",
			      vl_dev->data->vdev_data->init_notif_credits,
			      read_dev_credits(vl_dev),
			      read_dev_inters(vl_dev),
			      read_dev_notifs(vl_dev));

			/*
			 * We need to go to the next device if:
			 * a) Current device does not have available notifications AND
			 *    current device does not have available interrupts
			 * b) There is another pending device with higher priority
			 * c) There is another pending device in the same group
			 *    which has not been served for longer time.
			 */

			if (((read_dev_notifs(vl_dev) == 0) &&
			     (read_dev_inters(vl_dev) == 0)) ||
				(!dev_highest_prior_notifs(vl_dev)) ||
				(!oldest_active_dev_in_group(vl_dev))) {
				device = next_elem(device);
				continue;
			}

			pr_debug("Run Device %lu\n",
			      vl_dev->data->vdev_data->init_notif_credits);

			/*
			 * Keep the active highest priority in a variable
			 * and continue triggering notications only if the
			 * devices has priority equal or bigger then the highest.
			 * This helps to give control to the device with
			 * highest priority immediatly without waiting the
			 * running device to complete it turn.
			 */
			starting_time = ktime_get();
			deadline = ktime_add_ms(starting_time, read_dev_credits(vl_dev));
			while (ktime_before(starting_time, deadline) &&
						!kthread_should_stop() &&
						dev_highest_prior_notifs(vl_dev)) {
				if (read_dev_notifs(vl_dev) > 0) {
					trigger_dev_notif(vl_dev);
				} else if (read_dev_inters(vl_dev) > 0) {
					atomic_dec(&vl_dev->data->avail_inters);
					atomic_dec(&loopback_devices.pending_inters);

					vl_interrupt(vl_dev, 0);
				} else {
					/* Give some time for the current device */
					wait_event_timeout(
						vl_dev->wq_notifs_inters,
						(read_dev_notifs(vl_dev) > 0) ||
						(read_dev_inters(vl_dev) > 0) ||
							kthread_should_stop(),
						msecs_to_jiffies(5));
				}
				/* Update currnet time */
				starting_time = ktime_get();
			}

			/*
			 * If the device has not consumed its entire time,
			 * save the remaining credits for later usage.
			 */
			set_dev_credits(vl_dev, ktime_ms_delta(deadline, starting_time));
			if (read_dev_credits(vl_dev) == 0)
				reset_credits(vl_dev);

			device = next_elem(device);
			update_highest_active_prior_notifs();

		} else {
			wait_event_timeout(
				loopback_devices.wq_notifs_inters,
				((available_notifications() || available_interrupts()) &&
				!list_empty(&loopback_devices.virtio_devices_list)) ||
				kthread_should_stop(),
				100 * HZ);
		}
	}

sched_exit:
	pr_info("Clean any remaining devices\n");
	clean_all_devs();

	pr_info("Exiting notification thread\n");
	return 0;
}

/* The notify function used when creating a virtqueue */
static bool vl_notify(struct virtqueue *vq)
{
	struct virtio_loopback_device *vl_dev =
		to_virtio_loopback_device(vq->vdev);
	struct notify_data *data;

	pr_debug("VIRTIO_NOTIFY\n");

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

	pr_debug("Add notification for Device %lu avail credits: %u, avail notifications %u\n",
			vl_dev->data->vdev_data->init_notif_credits,
			read_dev_credits(vl_dev),
			read_dev_notifs(vl_dev));

	/*
	 * If device has priorities enabled, add the notification into
	 * the list and leave the notification thread to schedule it
	 * when this is appropriate.
	 */
	if (vl_dev->data->vdev_data->priority_enabled) {
		pr_debug("WAKEUP notification list\n");

		spin_lock(&vl_dev->notify_q_lock);
		if (vl_dev->data->priority_group >
		    atomic_read(&loopback_devices.highest_active_prior_notifs))
			atomic_set(&loopback_devices.highest_active_prior_notifs,
				vl_dev->data->priority_group);
		spin_unlock(&vl_dev->notify_q_lock);

		/* Update atomically the notification counters */
		atomic_inc(&vl_dev->data->avail_notifs);
		atomic_inc(&loopback_devices.pending_notifs);

		wake_up(&vl_dev->wq_notifs_inters);
		wake_up(&loopback_devices.wq_notifs_inters);

		return true;
	} else {
		return trigger_notification(vq);
	}
}


/* the interrupt function used when receiving an IRQ */
bool register_interrupt(struct virtio_loopback_device *vl_dev, int irq)
{
	pr_debug("Received an interrupt\n");
	if (vl_dev->data->vdev_data->priority_enabled) {

		pr_debug("Add notification for Device %lu avail credits: %u, avail inters %u\n",
				vl_dev->data->vdev_data->init_notif_credits,
				read_dev_credits(vl_dev),
				read_dev_inters(vl_dev));

		spin_lock(&vl_dev->notify_q_lock);
		if (vl_dev->data->priority_group >
		    atomic_read(&loopback_devices.highest_active_prior_notifs))
			atomic_set(&loopback_devices.highest_active_prior_notifs,
				vl_dev->data->priority_group);
		spin_unlock(&vl_dev->notify_q_lock);

		atomic_inc(&vl_dev->data->avail_inters);
		atomic_inc(&loopback_devices.pending_inters);

		pr_debug("WAKEUP interrupt list\n");
		wake_up(&vl_dev->wq_notifs_inters);
		wake_up(&loopback_devices.wq_notifs_inters);

		return true;
	} else {
		return vl_interrupt(vl_dev, irq);
	}
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

	pr_debug("virtio_loopback_release_dev\n");

	/* Deallocte platform data */
	devm_kfree(&pdev->dev, vl_dev);
}

static inline struct virtio_loopback_device *dev_to_vl(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return (struct virtio_loopback_device *)platform_get_drvdata(pdev);
}

static void vl_dev_sync_single_for_device(struct device *dev,
					     dma_addr_t dma_addr, size_t size,
					     enum dma_data_direction dir)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	pr_debug("vl_dev_sync_single_for_device\n");
	vl_domain_sync_single_for_device(domain, dma_addr, size, dir);

}

static void vl_dev_sync_single_for_cpu(struct device *dev,
					     dma_addr_t dma_addr, size_t size,
					     enum dma_data_direction dir)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	pr_debug("vl_dev_sync_single_for_cpu\n");
	vl_domain_sync_single_for_cpu(domain, dma_addr, size, dir);
}

static dma_addr_t vl_dev_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	return vl_domain_map_page(domain, page, offset, size, dir, attrs);
}

static void vl_dev_unmap_page(struct device *dev, dma_addr_t dma_addr,
				size_t size, enum dma_data_direction dir,
				unsigned long attrs)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	return vl_domain_unmap_page(domain, dma_addr, size, dir, attrs);
}

static void *vl_dev_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_addr, gfp_t flag,
					unsigned long attrs)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;
	unsigned long iova;
	void *addr;

	*dma_addr = DMA_MAPPING_ERROR;
	addr = vl_domain_alloc_coherent(domain, size,
				(dma_addr_t *)&iova, flag, attrs);
	if (!addr)
		return NULL;

	*dma_addr = (dma_addr_t)iova;

	return addr;
}

static void vl_dev_free_coherent(struct device *dev, size_t size,
					void *vaddr, dma_addr_t dma_addr,
					unsigned long attrs)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	vl_domain_free_coherent(domain, size, vaddr, dma_addr, attrs);
}

static size_t vl_dev_max_mapping_size(struct device *dev)
{
	struct virtio_loopback_device *vl_dev = dev_to_vl(dev);
	struct vl_iova_domain *domain = vl_dev->domain;

	return domain->bounce_size;
}

static const struct dma_map_ops vl_dev_dma_ops = {
	.sync_single_for_device = vl_dev_sync_single_for_device,
	.sync_single_for_cpu = vl_dev_sync_single_for_cpu,
	.map_page = vl_dev_map_page,
	.unmap_page = vl_dev_unmap_page,
	.alloc = vl_dev_alloc_coherent,
	.free = vl_dev_free_coherent,
	.max_mapping_size = vl_dev_max_mapping_size,
};

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

	vl_dev->bounce_size = VDUSE_BOUNCE_SIZE;

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

	/* Override DMA ops */
	set_dma_ops(&pdev->dev, &vl_dev_dma_ops);

	mutex_lock(&vl_dev->domain_lock);
	if (!vl_dev->domain)
		vl_dev->domain = vl_domain_create(VDUSE_IOVA_SIZE - 1,
						  vl_dev->bounce_size);
	mutex_unlock(&vl_dev->domain_lock);
	if (!vl_dev->domain) {
		put_device(&pdev->dev);
		return -ENOMEM;
	}

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

	/* Initialize the notifications related data structures */
	vl_dev->notify_workqueue =
		create_singlethread_workqueue("notify_workqueue");
	INIT_WORK(&vl_dev->notify_work, notify_work_handler);
	INIT_LIST_HEAD(&vl_dev->notify_list);
	spin_lock_init(&vl_dev->notify_q_lock);
	mutex_init(&vl_dev->domain_lock);
	init_waitqueue_head(&vl_dev->wq_notifs_inters);

	/* Set platform data */
	platform_set_drvdata(pdev, vl_dev);

	/* Insert new entry data */
	err = insert_entry_data(vl_dev, pdev->id);

out:
	return err;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 10, 8)
static void virtio_loopback_remove(struct platform_device *pdev)
#else
static int virtio_loopback_remove(struct platform_device *pdev)
#endif
{
	struct virtio_loopback_device *vl_dev;

	pr_debug("virtio_loopback_remove\n");
	vl_dev = platform_get_drvdata(pdev);

	if (vl_dev->data == NULL) {
		pr_debug("Dev already deallocated\n");
		return 0;
	}

	/* Destroy the notify workqueue */
	flush_workqueue(vl_dev->notify_workqueue);
	destroy_workqueue(vl_dev->notify_workqueue);

	if (vl_dev->data) {
		unregister_virtio_device(&vl_dev->vdev);
		pr_info("unregister_virtio_device!\n");
	}

	/* Subsequently free the device data */
	free_page((unsigned long)vl_dev->data->info->data);
	kfree(vl_dev->data->info);
	eventfd_ctx_put(vl_dev->data->efd_ctx);
	vl_dev->data->efd_ctx = NULL;
	kfree(vl_dev->data);
	vl_dev->data = NULL;

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
			100 * HZ);

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
			100 * HZ);

	mutex_unlock(&(dev_data)->read_write_lock);
}
