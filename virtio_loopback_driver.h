/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Virtio loopback device driver
 *
 * Copyright 2022-2024 Virtual Open Systems SAS.
 *
 * Authors:
 *  Timos Ampelikiotis <t.ampelikiotis@virtualopensystems.com>
 *  Anna Panagopoulou <anna@virtualopensystems.com>
 *  Alvise Rigo <a.rigo@virtualopensystems.com>
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
#include <linux/dma-map-ops.h>
#include <linux/swiotlb.h>
#include <linux/dma-direct.h>

/* vDUSE includes */
#include <linux/iova.h>
#include <linux/dma-mapping.h>
#include <linux/vhost_iotlb.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>

/* MMIO includes */
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>

#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>

/* mmap includes */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>

#include <linux/pagemap.h>
#include <linux/delay.h>

/* max Minor devices */
#define MAX_DEV	1
#define MAX_PDEV 100
#define PDEV_TYPES 2

/*
 * The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size.
 */
#define VIRTIO_MMIO_VRING_ALIGN PAGE_SIZE

#define to_virtio_loopback_device(ptr) \
	container_of(ptr, struct virtio_loopback_device, vdev)

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
#define SHARE_COM_STRUCT _IOC(_IOC_WRITE, 'k', 6, 0)
#define SHARE_VQS_NOTIF _IOC(_IOC_WRITE, 'k', 7, sizeof(struct vq_notifier))

/* Data structures */
struct virtio_device_info_struct {
	unsigned long magic;
	unsigned long version;
	unsigned long device_id;
	unsigned long vendor;
	bool priority_enabled;
	unsigned long init_notif_credits;
	unsigned long priority_group;
};

struct virtio_neg {
	uint64_t notification;
	uint64_t data;
	uint64_t size;
	bool read;
	atomic_t done;
};

struct mmap_data {
	bool share_communication_struct;
	bool share_vqs;
};

struct vq_notifier {
	uint32_t vq_index;
	int notifier_fd;
	int pid;
};

/* vq related data */
struct vq_data {
	uint32_t vq_index;
	uint64_t vq_pfns[16];
	uint64_t vq_pfn;
	struct eventfd_ctx *vq_notifiers[16];
	bool vq_notifiers_enabled;
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

	/* virtio device data */
	struct virtio_device_info_struct *vdev_data;
	bool priority_enabled;
	uint32_t priority_group;
	atomic_t notif_credits;
	atomic_t avail_notifs;
	atomic_t avail_inters;
	uint64_t served_timestamp;
};

/* Data describing each entry of the driver */
struct loopback_devices_array {
	/* Array of probed devices */
	struct virtio_loopback_device *devices[MAX_PDEV];
	/* list of the devices */
	struct list_head virtio_devices_list;
	/* Number of available devices */
	atomic_t device_num;
	/* Registration completion */
	struct completion reg_vl_dev_completion[MAX_PDEV];
	/* Counter for all devices pending notifications */
	atomic_t highest_active_prior_notifs;
	atomic_t pending_notifs;
	atomic_t pending_inters;
	wait_queue_head_t wq_notifs_inters;

	/* Spin lock for removing the device */
	spinlock_t running_lock;
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

struct vl_bounce_map {
	struct page *bounce_page;
	enum dma_data_direction dir;
	struct page *user_bounce_page;
	u64 orig_phys;
	u64 addr;
};

struct vl_iova_domain {
	struct iova_domain stream_iovad;
	struct iova_domain zc_iovad;
	struct iova_domain consistent_iovad;
	struct vl_bounce_map *bounce_maps;
	size_t bounce_size;
	unsigned long iova_limit;
	int bounce_map;
	struct vhost_iotlb *iotlb;
	spinlock_t iotlb_lock;
	struct file *file;
	bool user_bounce_pages;
	rwlock_t bounce_lock;
	struct vm_area_struct *vma;
};

struct virtio_loopback_device {
	struct virtio_device vdev;
	struct platform_device *pdev;
	/* Corresponding data pointer */
	struct device_data *data;

	/* SWIOTLB */
	struct vl_iova_domain *domain;

	/* Status: -1 not initialized, 0 running, 1 paused */
	int status;

	void __iomem *base;
	unsigned long version;

	/* A list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;

	/* Define workqueue for notifications */
	struct workqueue_struct *notify_workqueue;

	/* Notify list and work struct */
	spinlock_t notify_q_lock;
	struct list_head notify_list;
	struct work_struct notify_work;

	/* Notification waitqueue */
	wait_queue_head_t wq_notifs_inters;

	/* vduse attrs */
	unsigned int bounce_size;
	struct mutex domain_lock;
};

struct virtio_loopback_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;
	/* the list node for the virtqueues list */
	struct list_head node;
};

struct virtio_loopback_device_node {
	/* the actual virtqueue */
	uint32_t vq_index;
	atomic_t is_deleted;
	/* the list node for the virtqueues list */
	struct list_head node;
	struct rcu_head rcu;
};

/* Notify data*/
struct notify_data {
	uint32_t index;
	struct list_head list;
};

/* Interrupt data*/
struct interrupt_data {
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
	struct task_struct *notif_thread;
};

/* Global variables */
extern struct platform_driver virtio_loopback_driver;

/* Global functions */
int insert_entry_data(struct virtio_loopback_device *vl_dev, int id);
int loopback_register_virtio_dev(struct virtio_loopback_device *vl_dev);
bool register_interrupt(struct virtio_loopback_device *vl_dev, int irq);
int notif_sched_func(void *data);
bool add_dev_to_list(uint32_t array_dev_pos);
void note_dev_deletion(struct virtio_loopback_device *vl_dev);
extern struct loopback_devices_array loopback_devices;
extern struct loopback_device_data loopback_data;

/* vduse includes */
#define IOVA_START_PFN 1
#define INVALID_PHYS_ADDR (~(phys_addr_t)0)
#define VDUSE_MAX_BOUNCE_SIZE (1024 * 1024 * 1024)
#define VDUSE_MIN_BOUNCE_SIZE (1024 * 1024)
#define VDUSE_BOUNCE_SIZE (128 * 1024 * 1024)
/* 128 MB reserved for virtqueue creation */
#define VDUSE_IOVA_SIZE (VDUSE_MAX_BOUNCE_SIZE + 128 * 1024 * 1024)

void vl_domain_sync_single_for_device(struct vl_iova_domain *domain,
                                      dma_addr_t dma_addr, size_t size,
                                      enum dma_data_direction dir);
void vl_domain_sync_single_for_cpu(struct vl_iova_domain *domain,
                                      dma_addr_t dma_addr, size_t size,
                                      enum dma_data_direction dir);
dma_addr_t vl_domain_map_page(struct vl_iova_domain *domain,
                                 struct page *page, unsigned long offset,
                                 size_t size, enum dma_data_direction dir,
                                 unsigned long attrs);
void vl_domain_unmap_page(struct vl_iova_domain *domain,
                             dma_addr_t dma_addr, size_t size,
                             enum dma_data_direction dir, unsigned long attrs);
void *vl_domain_alloc_coherent(struct vl_iova_domain *domain,
                                  size_t size, dma_addr_t *dma_addr,
                                  gfp_t flag, unsigned long attrs);
void vl_domain_free_coherent(struct vl_iova_domain *domain, size_t size,
                                void *vaddr, dma_addr_t dma_addr,
                                unsigned long attrs);
void vl_domain_reset_bounce_map(struct vl_iova_domain *domain);
struct vl_iova_domain *vl_domain_create(unsigned long iova_limit,
                                              size_t bounce_size);
int vl_domain_init(void);
void vl_domain_exit(void);
int vl_domain_mmap(struct file *file, struct vm_area_struct *vma);
int vl_iotlb_add_range(struct vl_iova_domain *domain,
				 u64 start, u64 last,
				 u64 addr, unsigned int perm,
				 struct file *file, u64 offset);
#endif /* __LOOPBACK_H__ */
