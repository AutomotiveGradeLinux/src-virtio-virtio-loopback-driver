// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2022-2024 Virtual Open Systems SAS.
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
#ifndef __LOOPBACK_H__
#define __LOOPBACK_H__

#define DRIVER  "LOOPBACK"

#include <linux/cdev.h>
#include <linux/eventfd.h>
#include <linux/fdtable.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/version.h>
#include <linux/completion.h>

/* MMIO includes */
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_mmio.h>
#include <linux/virtio_ring.h>

#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>

/* mmap includes */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>

#include <linux/pagemap.h>
#include <linux/delay.h>

/* max Minor devices */
#define MAX_DEV	1
#define MAX_PDEV 100
#define PDEV_TYPES 2

/* Define mmap elements limit */
#define MMAP_LIMIT	200

/*
 * The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size.
 */
#define VIRTIO_MMIO_VRING_ALIGN PAGE_SIZE

#define to_virtio_loopback_device(ptr, field) \
	container_of(ptr, struct virtio_loopback_device, field)

/* mmap functionality */
#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

/* IOCTL defines */
#define EFD_INIT _IOC(_IOC_WRITE, 'k', 1, sizeof(efd_data))
#define WAKEUP _IOC(_IOC_WRITE, 'k', 2, 0)
#define START_LOOPBACK _IOC(_IOC_WRITE, 'k', 3, sizeof(struct virtio_device_info_struct))
#define IRQ _IOC(_IOC_WRITE, 'k', 4, sizeof(int))
#define SHARE_VQS _IOC(_IOC_WRITE, 'k', 5, sizeof(uint32_t))
#define SHARE_COM_STRUCT _IOC(_IOC_WRITE, 'k', 7, 0)

/* Data structures */
struct virtio_device_info_struct {
	unsigned long magic;
	unsigned long version;
	unsigned long device_id;
	unsigned long vendor;
};

struct virtio_neg {
	uint64_t notification;
	uint64_t data;
	uint64_t size;
	bool read;
	atomic_t done;
};

struct share_mmap {
	uint64_t pfn;
	uint64_t vm_start;
	uint32_t size;
	uint32_t uid;
	struct page *page;
};

struct mmap_data {
	int mmap_index;
	bool share_communication_struct;
	bool share_vqs;
	struct share_mmap share_mmap_list[MMAP_LIMIT];
	int cur_ram_idx;
	uint64_t sum_pgfaults;
};

/* vq related data */
struct vq_data {
	uint32_t vq_index;
	uint64_t vq_pfns[16];
	uint64_t vq_pfn;
};

/* Data describing each device private status */
struct device_data {
	/* Info needed for adapter ops */
	struct mmap_info *info;
	/* Waitqueue for the adapter */
	wait_queue_head_t wq;
	struct mutex read_write_lock;
	struct eventfd_ctx *efd_ctx;
	/*
	 * If this variable is true then read/write should wait
	 * the adapter to unlock this operation by sending an
	 * eventfd. If it's equal to "false" then the operation
	 * does not wait for adapter's confirmation.
	 */
	bool valid_eventfd;
	/* vq data */
	struct vq_data vq_data;
};

/* Data describing each entry of the driver */
struct loopback_devices_array {
	/* Array of probed devices */
	struct virtio_loopback_device *devices[MAX_PDEV];
	/* Number of available devices */
	atomic_t device_num;
	/* Registration completion */
	struct completion reg_vl_dev_completion[MAX_PDEV];
};

/* Data concealed in the file private pointer */
struct file_priv_data {
	/* Device needed data */
	struct device_data *dev_data;
	/* mmap needed data */
	struct mmap_data *mm_data;
	/* Device info! */
	struct virtio_device_info_struct device_info;
	/* The vl_dev pointer for the irq */
	struct virtio_loopback_device *vl_dev_irq;
};

struct virtio_loopback_device {
	struct virtio_device vdev;
	struct platform_device *pdev;
	/* Corresponding data pointer */
	struct device_data *data;

	/* Status: -1 not initialized, 0 running, 1 paused */
	int status;

	void __iomem *base;
	unsigned long version;

	/* A list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* Notify list and work struct */
	spinlock_t notify_q_lock;
	struct list_head notify_list;
	struct work_struct notify_work;
};

struct virtio_loopback_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;
	/* the list node for the virtqueues list */
	struct list_head node;
};

/* Notify data*/
struct notify_data {
	uint32_t index;
	struct list_head list;
};

/* Shared data structure between driver and user-space application */
struct mmap_info {
	void *data;
	int reference;
};

/*
 * This structure holds the eventfds shared between the driver
 * and the user-space application.
 */
struct efd_data {
	int efd[2];
	int pid;
};

/* device data holder, this structure may be extended to hold additional data */
struct loopback_device_data {
	/*device Major number */
	int dev_major;
	/* sysfs class structure */
	struct class *class;
	struct cdev cdev;
	/* Define workqueue for notifications */
	struct workqueue_struct *notify_workqueue;
};

/* Global variables */
extern struct loopback_device_data loopback_data;
extern struct loopback_devices_array loopback_devices;
extern struct platform_driver virtio_loopback_driver;

/* Global functions */
int insert_entry_data(struct virtio_loopback_device *vl_dev, int id);
int loopback_register_virtio_dev(struct virtio_loopback_device *vl_dev);
bool vl_interrupt(struct virtio_loopback_device *vl_dev, int irq);
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 10, 8)
void virtio_loopback_remove(struct platform_device *pdev);
#else
int virtio_loopback_remove(struct platform_device *pdev);
#endif

#endif /* __LOOPBACK_H__ */
