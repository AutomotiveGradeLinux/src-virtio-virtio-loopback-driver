// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio loopback device driver
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
 * This module is responsible to assign the virtio-loopback transport driver
 * to a group of virtio drivers in order to be able to share notifications and
 * the vrings (without copies) with the corresponding vhost-user devices in
 * the user-space.
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

#define pr_fmt(fmt) "virtio-loopback: " fmt

/* Loopback header file */
#include "virtio_loopback_driver.h"

/* Features */
MODULE_LICENSE("GPL");

/* The global data for the loopback */
struct loopback_device_data loopback_data;
struct loopback_devices_array loopback_devices;

/*
 * This function registers all mmap calls done by the user-space into an array
 */
static void add_share_mmap(struct file *filp, uint64_t pfn,
			   uint64_t vm_start, uint64_t size)
{
	struct file_priv_data *file_data =
		(struct file_priv_data *)(filp->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;

	mm_data->share_mmap_list[mm_data->mmap_index].pfn = pfn;
	mm_data->share_mmap_list[mm_data->mmap_index].vm_start = vm_start;
	mm_data->share_mmap_list[mm_data->mmap_index].size = size;
	mm_data->share_mmap_list[mm_data->mmap_index].uid =
		task_pid_nr(current);
	mm_data->mmap_index++;
}

/*
 * This function removes a record from mmap array
 */
static void share_mmap_rem(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct file_priv_data *file_data =
		(struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	int i;

	for (i = 0; i < MMAP_LIMIT; i++) {
		if (mm_data->share_mmap_list[i].vm_start == vma->vm_start) {
			mm_data->share_mmap_list[i].uid = 0;
			mm_data->share_mmap_list[i].pfn = 0;
			mm_data->share_mmap_list[i].vm_start = 0;
			mm_data->share_mmap_list[i].size = 0;
		}
	}
}

static void print_mmap_idx(struct mmap_data *mm_data, int i)
{
	pr_debug("share_mmap_list[%d].uid %x\n", i,
		 mm_data->share_mmap_list[i].uid);
	pr_debug("share_mmap_list[%d].pfn %llx\n", i,
		 mm_data->share_mmap_list[i].pfn);
	pr_debug("share_mmap_list[%d].vm_start %llx\n", i,
		 mm_data->share_mmap_list[i].vm_start);
	pr_debug("share_mmap_list[%d].size %x\n", i,
		 mm_data->share_mmap_list[i].size);
}

/**
 * print_mmaps - Debug function to print details of all active mmap entries
 * @mm_data: Pointer to the mmap_data structure containing mmap details
 *
 * This function iterates through the `share_mmap_list` array in the given
 * `mm_data` structure and logs the details of each active mmap entry by
 * calling `print_mmap_idx`. The number of entries printed is determined as:
 * - `MMAP_LIMIT` if `mmap_index` is `0`.
 * - The value of `mmap_index` otherwise.
 *
 * Note:
 * - The function uses `pr_debug` for logging, so enable debugging to see
 *   the output.
 * - Ensure that `mm_data` is properly initialized before calling this
 *   function to avoid accessing invalid memory.
 */

static void print_mmaps(struct mmap_data *mm_data)
{
	int i, limit =
		mm_data->mmap_index == 0 ? MMAP_LIMIT : mm_data->mmap_index;

	for (i = 0; i < limit; i++)
		print_mmap_idx(mm_data, i);
}

/**
 * share_mmap_exist_vma_return_correct_pfn - Calculate corrected PFN for a
 * given address.
 * @mm_data: Pointer to struct containing memory mapping data
 * @addr: Address for which to calculate the corrected PFN
 *
 * This function iterates through the list of shared memory mappings in
 * `mm_data` and checks if the given `addr` lies within any of the mappings.
 * If it does, it computes the corrected PFN based on the mapping's start
 * address, size, and PFN.
 *
 * Returns:
 * - The corrected PFN if the address falls within a mapping.
 * - 0 if the address does not match any mapping.
 */
static uint64_t share_mmap_exist_vma_return_correct_pfn(
					struct mmap_data *mm_data,
					uint64_t addr)
{
	int i;
	uint64_t corrected_pfn;

	for (i = 0; i < MMAP_LIMIT; i++) {
		if ((mm_data->share_mmap_list[i].vm_start <= addr) &&
		    (addr < mm_data->share_mmap_list[i].vm_start +
		    mm_data->share_mmap_list[i].size)) {
			corrected_pfn = ((addr -
					  mm_data->share_mmap_list[i].vm_start)
					 / PAGE_SIZE)
					+ mm_data->share_mmap_list[i].pfn;
			return corrected_pfn;
		}
	}
	return 0;
}

/**
 * pf_mmap_fault - Handle page faults for the device mmap area
 * @vmf: Pointer to the `vm_fault` structure containing fault information
 *
 * This function is called during a page fault to find and insert the correct
 * page for the faulting address. It calculates the corrected PFN using the
 * provided mmap data of the device and updates the faulting page.
 *
 * Returns:
 * - 0 if successful.
 * - `VM_FAULT_SIGBUS` on failure.
 */
static vm_fault_t pf_mmap_fault(struct vm_fault *vmf)
{
	uint64_t corrected_pfn;
	struct page *page;

	struct file *file = vmf->vma->vm_file;
	struct file_priv_data *file_data =
		(struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data =
		(struct mmap_data *)file_data->mm_data;

	/* Count the total number of page_faults for debugging purpose */
	mm_data->sum_pgfaults++;

	/* Find the corrected pfn */
	corrected_pfn = share_mmap_exist_vma_return_correct_pfn(mm_data,
								vmf->address);

	/* Ensure the PFN is valid */
	if (unlikely(!pfn_valid(corrected_pfn))) {
		pr_err("Invalid PFN: %llu\n", corrected_pfn);
		return VM_FAULT_SIGBUS;
	}

	/* After finding the page, correct the vmf->page */
	page = pfn_to_page(corrected_pfn);
	if (unlikely(!virt_addr_valid(page_address(page)))) {
		pr_err("Invalid page address for PFN: %llu\n", corrected_pfn);
		return VM_FAULT_SIGBUS;
	}

	/* Insert the correct page */
	return vmf_insert_pfn(vmf->vma, vmf->address, corrected_pfn);
}

static void pf_mmap_close(struct vm_area_struct *vma)
{
	share_mmap_rem(vma);
}

const struct vm_operations_struct pf_mmap_ops = {
	.close = pf_mmap_close,
	.fault = pf_mmap_fault,
};

/**
 * pf_mmap_vm_page - Set up memory mapping for a file
 * @filp: Pointer to the file structure for the mapping
 * @vma: Pointer to the VM area structure representing the memory mapping
 *
 * This function sets up a user-space area by associating a physical frame
 * number (PFN) with the virtual address range. It updates internal data
 * structures to track the mapping and sets appropriate VM flags.
 *
 * Returns:
 * - 0 on success.
 * - Negative error code on failure.
 */
static int pf_mmap_vm_page(struct file *filp, struct vm_area_struct *vma)
{
	uint64_t size = (unsigned long)(vma->vm_end - vma->vm_start);
	struct file_priv_data *file_data =
		(struct file_priv_data *)(filp->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	uint64_t pfn = ((mm_data->cur_ram_idx++) * (size >> PAGE_SHIFT));

#if LINUX_VERSION_CODE <  KERNEL_VERSION(6, 3, 0)
		vma->vm_flags |= VM_PFNMAP;
#else
		vm_flags_set(vma, VM_PFNMAP);
#endif
	add_share_mmap(filp, pfn, vma->vm_start, size);
	return 0;
}

/**
 * mmap_vqs_com_struct - Map virtqueue or communication structure to user space
 * @filp: Pointer to the file structure associated with the mapping
 * @vma: Pointer to the VM area structure describing the memory region
 *
 * This function maps either the virtqueue data or the communication structure
 * to the user space using `remap_pfn_range`. The choice of what to map depends
 * on the `share_communication_struct` flag in the mmap data structure.
 *
 * Returns:
 * - 0 on success.
 * - Negative error code on failure.
 */
static int mmap_vqs_com_struct(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	struct file_priv_data *file_data =
		(struct file_priv_data *)(filp->private_data);
	struct device_data *dev_data =
		(struct device_data *)file_data->dev_data;
	struct mmap_data *mmap_data = (struct mmap_data *)file_data->mm_data;
	struct mmap_info *com_mmap_virt =
		(struct mmap_info *)(file_data->dev_data->info)->data;
	uint64_t com_mmap_pfn =
		((uint64_t)virt_to_phys(com_mmap_virt)) >> PAGE_SHIFT;
	uint64_t starting_pfn;

	if (mmap_data->share_communication_struct) {
#if LINUX_VERSION_CODE <  KERNEL_VERSION(5, 16, 0)
		vma->vm_flags |= VM_RESERVED;
#else
		vm_flags_set(vma, VM_RESERVED);
#endif
		mmap_data->share_communication_struct = false;
		starting_pfn = com_mmap_pfn;
	} else {
		mmap_data->share_vqs = false;
		starting_pfn = dev_data->vq_data.vq_pfn;
	}

	ret = remap_pfn_range(vma, vma->vm_start, starting_pfn, size,
			      vma->vm_page_prot);
	if (ret != 0) {
		pr_err("Mmap error\n");
		print_mmaps(mmap_data);
	} else {
		add_share_mmap(filp, starting_pfn, vma->vm_start, size);
	}

	return ret;
}

/**
 * op_mmap - Map vring buffers, virtqueue or communication structure
 *           to user space.
 * @filp: Pointer to the file structure associated with the mapping
 * @vma: Pointer to the VM area structure describing the memory region
 *
 * This function checks if the incoming mmap sys_call is related to a) vrings
 * or b) virtqueues / communication structure data (depending on
 * `share_communication_struct` and `share_vqs` variables. Then calls
 * `mmap_vqs_com_struct` and `pf_mmap_vm_page` correspondingly in order
 * to apply a different mapping logic.
 *
 * Returns:
 * - 0 on success.
 * - Negative error code on failure.
 */
static int op_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct file_priv_data *file_data =
		(struct file_priv_data *)(filp->private_data);
	struct mmap_data *mmap_data = (struct mmap_data *)file_data->mm_data;
	int ret = 0;

	vma->vm_ops = &pf_mmap_ops;

	if (mmap_data->share_communication_struct || mmap_data->share_vqs)
		ret = mmap_vqs_com_struct(filp, vma);
	else
		ret = pf_mmap_vm_page(filp, vma);

	return ret;
}

static ssize_t loopback_write(struct file *file,
			      const char __user *user_buffer,
			      size_t size,
			      loff_t *offset)
{
	ssize_t len = sizeof(int);

	if (len <= 0)
		return 0;

	return len;
}

static ssize_t loopback_read(struct file *file,
			     char __user *user_buffer,
			     size_t size, loff_t *offset)
{
	return 0;
}

/*
 * The lseek sys_call is needed only by the vhost-user device
 * located in vhost-device crate.
 */
static loff_t loopback_seek(struct file *file, loff_t offset, int whence)
{
	loff_t new_pos;

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

static int register_virtio_loopback_dev(uint32_t device_id)
{
	struct platform_device *pdev;
	int err = 0;

	pr_info("Received request to register a new loopback transport\n");

	/* Register a new loopback-transport device */
	pdev = platform_device_register_simple("loopback-transport",
					       device_id, NULL, 0);
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		pr_err("Failed to register transport device: %d\n", err);
	}

	return err;
}

/* Insert new entry data for a discovered device */
int insert_entry_data(struct virtio_loopback_device *vl_dev, int id)
{
	int err = 0;
	/* Read that value atomically */
	uint32_t max_used_dev_idx = atomic_read(&loopback_devices.device_num);

	/* Store the new vl_dev */
	if ((id <= MAX_PDEV) && (max_used_dev_idx < MAX_PDEV))
		loopback_devices.devices[id] = vl_dev;
	else
		err = -ENOMEM;

	/* Mark the request as completed and free registration */
	complete(&loopback_devices.reg_vl_dev_completion[id]);
	return err;
}

/* Helper function to mark an entry as active */
static struct virtio_loopback_device *
activate_entry_data(struct file_priv_data *file_data,
		    uint32_t curr_dev_id)
{
	struct virtio_loopback_device *vl_dev = NULL;

	/* See if there is any available device */
	if (curr_dev_id < MAX_PDEV) {
		/* Find and store the data */
		vl_dev = loopback_devices.devices[curr_dev_id];
		vl_dev->data = file_data->dev_data;
		vl_dev->data->vdev_data = &file_data->device_info;

		/* Add this device to a global list */
		if (!add_dev_to_list(curr_dev_id))
			return NULL;

		/* Set credits & last served timestamp */
		atomic_set(&vl_dev->data->notif_credits,
			vl_dev->data->vdev_data->init_notif_credits);
		vl_dev->data->served_timestamp = ktime_get();

		/* Set available notifs */
		atomic_set(&vl_dev->data->avail_notifs, 0);

		/* Set device group */
		vl_dev->data->priority_group = vl_dev->data->vdev_data->priority_group;

		/* Set available interupts */
		atomic_set(&vl_dev->data->avail_inters, 0);
	}

	return vl_dev;
}

static int start_loopback(struct file_priv_data *file_data,
			  uint32_t curr_dev_id)
{
	struct virtio_loopback_device *vl_dev;
	int ret;

	/* Activate the entry */
	vl_dev = activate_entry_data(file_data, curr_dev_id);
	if (vl_dev) {
		file_data->vl_dev_irq = vl_dev;
		/* Register the activated vl_dev in the system */
		ret = loopback_register_virtio_dev(vl_dev);
	} else {
		pr_debug("No available entry found!\n");
		file_data->vl_dev_irq = NULL;
		ret = -EFAULT;
	}

	return ret;
}

/**
 * loopback_ioctl - Handle various ioctl commands for loopback device
 * @file: Pointer to the file structure associated with the device
 * @cmd: The ioctl command code
 * @arg: User-space argument associated with the command
 *
 * This function processes various ioctl commands to configure and control the
 * loopback device. The supported commands include:
 *
 * - `EFD_INIT`: The user-space adapter component shares an eventfd with the
 *   loopback device. This eventfd is triggered by the device each time a
 *   read / write operation is requested via the communication data structure.
 *
 * - `WAKEUP`: Sets a flag in the device's internal structure and wakes up any
 *   read / write process waiting on the communication wait queue.
 *
 * - `START_LOOPBACK`: Registers and starts a new loopback device, assigning a
 *   unique device ID and waiting for its probe function to complete before
 *   returning to user space.
 *
 * - `IRQ`: Handles an interrupt request by triggering the device's interrupt
 *   logic with the provided IRQ number.
 *
 * - `SHARE_VQS`: Shares a specified virtqueue (selected via a queue index)
 *   between the user-space application and the loopback device.
 *
 * - `SHARE_COM_STRUCT`: Notifies the loopback-device that the next mmap call
 *   will request the communication structure to be as shared between
 *   user-space and the loopback device.
 *
 * - `SHARE_VQS_NOTIF`: The user-space uses this command to share the eventfd
 *   associated with a specific virtqueue. This eventfd will be triggered each
 *   time the virtio device calls the `notify` function. In this way the
 *   by-pass the user-space adapter component and delivered directly to the
 *   vhost-user devices in user-space.
 *
 * If an unknown `cmd` is provided, the function logs an error and returns
 * `-ENOTTY` to indicate an unsupported ioctl command.
 *
 * Returns:
 * - `0` on success.
 * - Negative error codes (`-EFAULT`, `-ENOTTY`, or others) on failure.
 */
static long loopback_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct efd_data efd_data;
	int irq, err;
	uint32_t queue_sel;
	struct file_priv_data *file_data =
		(struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	struct device_data *dev_data =
		(struct device_data *)file_data->dev_data;
	uint32_t curr_avail_dev_id;
	struct vq_notifier vq_notifier;

	switch (cmd) {
	case EFD_INIT: {
		struct task_struct *userspace_task;
		struct file *efd_file;

		if (copy_from_user(&efd_data, (struct efd_data *) arg,
						   sizeof(struct efd_data)))
			return -EFAULT;

		userspace_task = pid_task(find_vpid(efd_data.pid), PIDTYPE_PID);

		rcu_read_lock();
#if LINUX_VERSION_CODE <  KERNEL_VERSION(5, 11, 220)
		efd_file = fcheck_files(userspace_task->files, efd_data.efd[0]);
#else
#if LINUX_VERSION_CODE <  KERNEL_VERSION(6, 7, 0)
		efd_file = files_lookup_fd_rcu(userspace_task->files, efd_data.efd[0]);
#else
		efd_file = files_lookup_fd_raw(userspace_task->files, efd_data.efd[0]);
#endif
#endif
		rcu_read_unlock();

		dev_data->efd_ctx = eventfd_ctx_fileget(efd_file);
		if (!dev_data->efd_ctx)
			return -1;

		break;
	}
	case WAKEUP: {
		atomic_set(&((struct virtio_neg *)(dev_data->info->data))->done,
			   1);
		wake_up(&(dev_data)->wq);
		break;
	}
	case START_LOOPBACK: {
		if (copy_from_user(&(file_data)->device_info,
				   (struct virtio_device_info_struct *) arg,
				   sizeof(struct virtio_device_info_struct)))
			return -EFAULT;

		pr_crit("Priority: %lu\n", file_data->device_info.init_notif_credits);
		/* Read and increase that value atomically */
		curr_avail_dev_id =
			atomic_add_return(1, &loopback_devices.device_num) - 1;

		/* Register a new loopback device */
		err = register_virtio_loopback_dev(curr_avail_dev_id);
		if (err)
			return -EFAULT;

		/*
		 * Wait for probe function to be called before return control
		 * to user-space app
		 */
		wait_for_completion(
			&loopback_devices.reg_vl_dev_completion[curr_avail_dev_id]);

		/* Start the loopback */
		err = start_loopback(file_data, curr_avail_dev_id);
		if (err)
			return -EFAULT;

		break;
	}
	case IRQ:
		if (copy_from_user(&irq, (int *) arg, sizeof(int)))
			return -EFAULT;
		register_interrupt(file_data->vl_dev_irq, irq);
		break;
	case SHARE_VQS:
		if (copy_from_user(&queue_sel, (uint32_t *) arg,
				   sizeof(uint32_t)))
			return -EFAULT;
		dev_data->vq_data.vq_pfn = dev_data->vq_data.vq_pfns[queue_sel];
		mm_data->share_vqs = true;
		break;
	case SHARE_COM_STRUCT:
		mm_data->share_communication_struct = true;
		break;
	case SHARE_VQS_NOTIF:

		struct task_struct *userspace_task;
		struct file *efd_file;

		if (copy_from_user(&vq_notifier, (struct vq_notifier *) arg,
				   sizeof(struct vq_notifier)))
			return -EFAULT;

		userspace_task =
			pid_task(find_vpid(vq_notifier.pid), PIDTYPE_PID);

		rcu_read_lock();
#if LINUX_VERSION_CODE <  KERNEL_VERSION(5, 11, 220)
				efd_file = fcheck_files(userspace_task->files, vq_notifier.notifier_fd);
#else
#if LINUX_VERSION_CODE <  KERNEL_VERSION(6, 7, 0)
				efd_file = files_lookup_fd_rcu(userspace_task->files, vq_notifier.notifier_fd);
#else
				efd_file = files_lookup_fd_raw(userspace_task->files, vq_notifier.notifier_fd);
#endif
#endif
		rcu_read_unlock();

		dev_data->vq_data.vq_notifiers[vq_notifier.vq_index] =
			eventfd_ctx_fileget(efd_file);
		if (!dev_data->vq_data.vq_notifiers[vq_notifier.vq_index])
			return -1;
		/* Mark device notifiers as enabled */
		dev_data->vq_data.vq_notifiers_enabled = true;
		break;
	default:
		pr_err("Unknown loopback ioctl: %u\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

static int loopback_open(struct inode *inode, struct file *file)
{
	uint32_t val_1gb = 1024 * 1024 * 1024;
	struct virtio_neg device_neg = {.done = ATOMIC_INIT(0)};
	/* Allocate file private data */
	struct file_priv_data *file_data =
		kmalloc(sizeof(struct file_priv_data), GFP_KERNEL);
	struct device_data *dev_data =
		kmalloc(sizeof(struct device_data), GFP_KERNEL);
	struct mmap_data *mm_data =
		kmalloc(sizeof(struct mmap_data), GFP_KERNEL);

	if (!file_data || !dev_data || !mm_data)
		goto error_kmalloc;

	/* Set the i_size for the stat SYS_CALL*/
	file->f_inode->i_size = 10 * val_1gb;

	/* Initialize the device data */
	dev_data->info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);
	if (!dev_data->info)
		goto error_kmalloc;
	dev_data->info->data = (void *)get_zeroed_page(GFP_KERNEL);
	memcpy(dev_data->info->data, &device_neg, sizeof(struct virtio_neg));

	/* Init wq */
	init_waitqueue_head(&(dev_data)->wq);

	/* Init mutex */
	mutex_init(&(dev_data)->read_write_lock);

	/* Init vq_data */
	dev_data->vq_data.vq_index = 0;
	dev_data->valid_eventfd = true;
	dev_data->vq_data.vq_notifiers_enabled = false;
	file_data->dev_data = dev_data;

	/* Init file mmap_data */
	mm_data->mmap_index = 0;
	mm_data->share_communication_struct = false;
	mm_data->share_vqs = false;
	mm_data->cur_ram_idx = 0;
	mm_data->sum_pgfaults = 0;
	file_data->mm_data = mm_data;

	/* Store in the private data as it should */
	file->private_data = (struct file_priv_data *)file_data;

	return 0;

error_kmalloc:
		kfree(file_data);
		kfree(dev_data);
		kfree(mm_data);
	return -ENOMEM;
}

static int start_notif_thread(void)
{
	loopback_data.notif_thread = kthread_run(notif_sched_func, NULL,
						    "notif_thread");
	if (IS_ERR(loopback_data.notif_thread)) {
		pr_err("Failed to create kernel thread\n");
		return PTR_ERR(loopback_data.notif_thread);
	}

	pr_info("Kernel notif thread started successfully\n");
	return 0;
}

static int loopback_release(struct inode *inode, struct file *file)
{
	struct file_priv_data *file_data =
		(struct file_priv_data *)(file->private_data);
	struct device_data *dev_data =
		(struct device_data *)file_data->dev_data;
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;

	pr_info("Releasing the device\n");

	/* Unregister from the list */
	note_dev_deletion(file_data->vl_dev_irq);

	/*
	 * This makes the read/write do not wait
	 * for the virtio-loopback-adapter if
	 * the last has closed the fd
	 */
	dev_data->valid_eventfd = false;

	/* Active entry found */
	if (file_data->vl_dev_irq) {
		pr_debug("About to cancel the work\n");
		/* TODO: Move this into virtio_loopback_remove */
		/* Cancel any pending work */
		cancel_work_sync(&file_data->vl_dev_irq->notify_work);
		/* Continue with the vl_dev unregister */
		platform_device_unregister(file_data->vl_dev_irq->pdev);
		file_data->vl_dev_irq = NULL;
	}

	/* Proceed to de-activating the data for this entry */
	dev_data = NULL;

	/* Continue with the mm_data */
	kfree(mm_data);
	file_data->mm_data = NULL;

	/* Last, free the private data */
	kfree(file_data);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations fops = {
	.owner			= THIS_MODULE,
	.read			= loopback_read,
	.write			= loopback_write,
	.open			= loopback_open,
	.unlocked_ioctl		= loopback_ioctl,
	.mmap			= op_mmap,
	.llseek			= loopback_seek,
	.release		= loopback_release
};

static int __init loopback_init(void)
{
	int err, i;
	dev_t dev;

	err = alloc_chrdev_region(&dev, 0, MAX_DEV, "loopback");

	/* Set-up the loopback_data */
	loopback_data.dev_major = MAJOR(dev);
#if LINUX_VERSION_CODE <  KERNEL_VERSION(6, 4, 0)
	loopback_data.class = class_create(THIS_MODULE, "loopback");
#else
	loopback_data.class = class_create("loopback");
#endif
	if (IS_ERR(loopback_data.class)) {
		pr_err("Failed to create class\n");
		return PTR_ERR(loopback_data.class);
	}
	cdev_init(&loopback_data.cdev, &fops);
	loopback_data.cdev.owner = THIS_MODULE;
	cdev_add(&loopback_data.cdev, MKDEV(loopback_data.dev_major, 0), 1);
	device_create(loopback_data.class, NULL,
		      MKDEV(loopback_data.dev_major, 0), NULL, "loopback");

	/* Register virtio_loopback_transport */
	(void)platform_driver_register(&virtio_loopback_driver);

	/* Init loopback device array */
	atomic_set(&loopback_devices.device_num, 1);

	/* Init completion for all devices */
	for (i = 0; i < MAX_PDEV; i++)
		init_completion(&loopback_devices.reg_vl_dev_completion[i]);

	/* Init loopback device list */
	INIT_LIST_HEAD(&loopback_devices.virtio_devices_list);

	/* Init notification / interrupt wait queue */
	init_waitqueue_head(&loopback_devices.wq_notifs_inters);

	/* Init spinlock for when device is running */
	spin_lock_init(&loopback_devices.running_lock);

	/* Init pending notifications counter */
	atomic_set(&loopback_devices.pending_notifs, 0);

	/* Init pending interrupts counter */
	atomic_set(&loopback_devices.pending_inters, 0);

	/* Init current highest notifications priority */
	atomic_set(&loopback_devices.highest_active_prior_notifs, 0);

	/* Start nofication thread */
	return start_notif_thread();
}

static void __exit loopback_exit(void)
{
	int ret;

	pr_info("Exit virtio_loopback driver!\n");

	/* Wait for notification / interrupt thread to stop */
	if (loopback_data.notif_thread) {
		ret = kthread_stop(loopback_data.notif_thread);
		if (ret) {
			pr_err("Kernel notif thread returned error: %d\n"
			       , ret);
		}
	}

	/* Unregister virtio_loopback_transport */
	platform_driver_unregister(&virtio_loopback_driver);
	pr_debug("platform_driver_unregister!\n");

	/* Necessary actions for the loopback_data */
	device_destroy(loopback_data.class, MKDEV(loopback_data.dev_major, 0));
	cdev_del(&loopback_data.cdev);
	pr_debug("device_destroy!\n");
	class_destroy(loopback_data.class);
	pr_debug("class_destroy!\n");
}

module_init(loopback_init);
module_exit(loopback_exit);
