// SPDX-License-Identifier: GPL-2.0-or-later
/*
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

/* Loopback header file */
#include "virtio_loopback_driver.h"

/* Features */
MODULE_LICENSE("GPL v2");

/* The global data for the loopback */
struct loopback_device_data loopback_data;
struct loopback_devices_array loopback_devices;

/*
 * This functions registers all mmap calls done by the user-space into an array
 */
static void add_share_mmap(struct file *filp, uint64_t pfn, uint64_t vm_start, uint64_t size)
{
	struct file_priv_data *file_data = (struct file_priv_data *)(filp->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;

	pr_debug("Add new mmaping! index: %d\n", mm_data->mmap_index);
	pr_debug("pfn: 0x%llx", pfn);
	pr_debug("vm_start: 0x%llx", vm_start);
	pr_debug("size: 0x%llx", size);

	mm_data->share_mmap_list[mm_data->mmap_index].pfn = pfn;
	mm_data->share_mmap_list[mm_data->mmap_index].vm_start = vm_start;
	mm_data->share_mmap_list[mm_data->mmap_index].size = size;
	mm_data->share_mmap_list[mm_data->mmap_index].uid = task_pid_nr(current);
	mm_data->mmap_index++;
}

/*
 * This functions removes a record from mmap array
 */
static void share_mmap_rem(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct file_priv_data *file_data = (struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	int i;

	for (i = 0; i < MMAP_LIMIT; i++) {
		if (mm_data->share_mmap_list[i].vm_start == vma->vm_start) {
			pr_debug("share_mmap with pa: 0x%llx and size: %x is deleted from the list\n",
					 mm_data->share_mmap_list[i].pfn, mm_data->share_mmap_list[i].size);
			mm_data->share_mmap_list[i].uid = 0;
			mm_data->share_mmap_list[i].pfn = 0;
			mm_data->share_mmap_list[i].vm_start = 0;
			mm_data->share_mmap_list[i].size = 0;
		}
	}
}

static void print_mmap_idx(struct mmap_data *mm_data, int i)
{
	pr_debug("share_mmap_list[%d].uid %x\n", i, mm_data->share_mmap_list[i].uid);
	pr_debug("share_mmap_list[%d].pfn %llx\n", i, mm_data->share_mmap_list[i].pfn);
	pr_debug("share_mmap_list[%d].vm_start %llx\n", i, mm_data->share_mmap_list[i].vm_start);
	pr_debug("share_mmap_list[%d].size %x\n", i, mm_data->share_mmap_list[i].size);
}

static void print_mmaps(struct mmap_data *mm_data)
{
	int i, limit = mm_data->mmap_index == 0 ? MMAP_LIMIT : mm_data->mmap_index;

	for (i = 0; i < limit; i++)
		print_mmap_idx(mm_data, i);
}

/*
 * This function return the corresponding user-space address of a pfn
 * based on the mapping done during the initialization
 */
static uint64_t share_mmap_exist_vma_return_correct_pfn(struct mmap_data *mm_data, uint64_t addr)
{
	int i;
	uint64_t corrected_pfn;

	for (i = 0; i < MMAP_LIMIT; i++) {
		if ((mm_data->share_mmap_list[i].vm_start <= addr) &&
			(addr < mm_data->share_mmap_list[i].vm_start + mm_data->share_mmap_list[i].size)) {
			pr_debug("addr (0x%llx) exist in: 0x%llx - 0x%llx\n", addr, mm_data->share_mmap_list[i].vm_start,
					mm_data->share_mmap_list[i].vm_start + mm_data->share_mmap_list[i].size);
			pr_debug("((addr - share_mmap_list[i].vm_start) / PAGE_SIZE): 0x%llx\n",
																			((addr - mm_data->share_mmap_list[i].vm_start) / PAGE_SIZE));
			pr_debug("share_mmap_list[i].pfn: 0x%llx\n", mm_data->share_mmap_list[i].pfn);
			corrected_pfn = ((addr - mm_data->share_mmap_list[i].vm_start) / PAGE_SIZE) + mm_data->share_mmap_list[i].pfn;
			return corrected_pfn;
		}
	}
	return 0;
}

static void pf_mmap_close(struct vm_area_struct *vma)
{
	pr_debug("unmap\t-> vma->vm_start: 0x%lx\n", vma->vm_start);
	pr_debug("unmap\t-> size: %lu\n", vma->vm_end - vma->vm_start);
	share_mmap_rem(vma);
}

static vm_fault_t pf_mmap_fault(struct vm_fault *vmf)
{
	uint64_t corrected_pfn;
	pfn_t corr_pfn_struct;
	struct page *page;
	int ret = 0;

	struct file *file = vmf->vma->vm_file;
	struct file_priv_data *file_data = (struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;

	pr_debug("----- Page fault: %lld -----\n", mm_data->sum_pgfaults);
	mm_data->sum_pgfaults++;

	/* Find the corrected pfn */
	corrected_pfn = share_mmap_exist_vma_return_correct_pfn(mm_data, vmf->address);
	corr_pfn_struct.val = corrected_pfn;

	/* Some debug prints */
	pr_debug("vma->vm_start: 0x%lx\n", vmf->vma->vm_start);
	pr_debug("vma->vm_pgoff: 0x%lx\n", vmf->vma->vm_pgoff);
	pr_debug("vmf->address: 0x%lx\n", vmf->address);
	pr_debug("corrected_pfn: 0x%llx\n", corrected_pfn);
	pr_debug("pfn_valid(corrected_pfn): 0x%x\n", pfn_valid(corrected_pfn));

	BUG_ON(!pfn_valid(corrected_pfn));

	/* After finding the page, correct the vmf->page */
	page = pfn_to_page(corrected_pfn);
	BUG_ON(!virt_addr_valid(page_address(page)));

	/* Insert the correct page */
	ret = vmf_insert_pfn(vmf->vma, vmf->address, corrected_pfn);
	pr_debug("vmf_insert_pfn -> ret: %d\n", ret);

	return ret;
}

const struct vm_operations_struct pf_mmap_ops = {
	.close = pf_mmap_close,
	.fault = pf_mmap_fault,
};

static int pf_mmap_vm_page(struct file *filp, struct vm_area_struct *vma)
{
	uint64_t size = (unsigned long)(vma->vm_end - vma->vm_start);
	struct file_priv_data *file_data = (struct file_priv_data *)(filp->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	uint64_t pfn = ((mm_data->cur_ram_idx++) * 0x40000);

#if LINUX_VERSION_CODE <  KERNEL_VERSION(6, 3, 0)
		vma->vm_flags |= VM_PFNMAP;
#else
		vm_flags_set(vma, VM_PFNMAP);
#endif
	add_share_mmap(filp, pfn, vma->vm_start, size);
	return 0;
}

static int mmap_vqs_com_struct(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	struct file_priv_data *file_data = (struct file_priv_data *)(filp->private_data);
	struct device_data *dev_data = (struct device_data *)file_data->dev_data;
	struct mmap_data *mmap_data = (struct mmap_data *)file_data->mm_data;
	struct mmap_info *com_mmap_virt = (struct mmap_info *)(file_data->dev_data->info)->data;
	uint64_t com_mmap_pfn = ((uint64_t)virt_to_phys(com_mmap_virt)) >> PAGE_SHIFT;
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

	ret = remap_pfn_range(vma, vma->vm_start, starting_pfn, size, vma->vm_page_prot);
	if (ret != 0) {
		pr_err("Mmap error\n");
		print_mmaps(mmap_data);
		goto out;
	}

	add_share_mmap(filp, starting_pfn, vma->vm_start, size);

out:
	return ret;
}

static int op_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct file_priv_data *file_data = (struct file_priv_data *)(filp->private_data);
	struct mmap_data *mmap_data = (struct mmap_data *)file_data->mm_data;
	int ret = 0;

	pr_debug("MMAP SYS_CALL -> vma->vm_pgoff: 0x%lx", vma->vm_pgoff);
	vma->vm_ops = &pf_mmap_ops;

	if (mmap_data->share_communication_struct || mmap_data->share_vqs) {
		ret = mmap_vqs_com_struct(filp, vma);
		goto out;
	}

	ret = pf_mmap_vm_page(filp, vma);

out:
	return ret;
}

/* Defined for future work */
static ssize_t loopback_write(struct file *file,
				const char __user *user_buffer,
				size_t size,
				loff_t *offset)
{
	ssize_t len = sizeof(int);

	pr_debug("loopback write function\n");
	if (len <= 0)
		return 0;

	return len;
}

/* Defined for future work */
static ssize_t loopback_read(struct file *file,
				 char __user *user_buffer,
				 size_t size, loff_t *offset)
{
	pr_debug("loopback read function\n");
	return 0;
}

static loff_t loopback_seek(struct file *file, loff_t offset, int whence)
{
	loff_t new_pos;

	pr_debug("loopback seek function!\n");
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

	pr_info("Received request to register a new virtio-loopback-dev\n");

	/* Register a new loopback-transport device */
	pdev = platform_device_register_simple("loopback-transport", device_id, NULL, 0);
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		pr_err("failed to register loopback-transport device: %d\n", err);
	}

	return err;
}

/* Insert new entry data for a discovered device */
int insert_entry_data(struct virtio_loopback_device *vl_dev, int id)
{
	int err = 0;
	/* Read and that value atomically */
	uint32_t max_used_dev_idx = atomic_read(&loopback_devices.device_num);

	/* Store the new vl_dev */
	if ((id <= MAX_PDEV) && (max_used_dev_idx < MAX_PDEV)) {
		loopback_devices.devices[id] = vl_dev;
	} else {
		err = -ENOMEM;
	}

	/* Mark the request as completed and free registration */
	complete(&loopback_devices.reg_vl_dev_completion[id]);
	return err;
}

/* Helper function to mark an entry as active */
static struct virtio_loopback_device *activate_entry_data(struct device_data *data, uint32_t curr_dev_id)
{
	struct virtio_loopback_device *vl_dev = NULL;

	/* See if there is any available device */
	if (curr_dev_id < MAX_PDEV) {
		/* Find and store the data */
		vl_dev = loopback_devices.devices[curr_dev_id];
		vl_dev->data = data;
	}

	return vl_dev;
}

static int start_loopback(struct file_priv_data *file_data, uint32_t curr_dev_id)
{
	struct virtio_loopback_device *vl_dev;
	int rc;

	/* Activate the entry */
	vl_dev = activate_entry_data(file_data->dev_data, curr_dev_id);
	if (vl_dev) {
		file_data->vl_dev_irq = vl_dev;
		/* Register the activated vl_dev in the system */
		rc = loopback_register_virtio_dev(vl_dev);
	} else {
		pr_debug("No available entry found!\n");
		file_data->vl_dev_irq = NULL;
		rc = -EFAULT;
	}

	return rc;
}

static long loopback_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	struct efd_data efd_data;
	int irq, err;
	uint32_t queue_sel;
	struct file_priv_data *file_data = (struct file_priv_data *)(file->private_data);
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;
	struct device_data *dev_data = (struct device_data *)file_data->dev_data;
	uint32_t curr_avail_dev_id;

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
		atomic_set(&((struct virtio_neg *)(dev_data->info->data))->done, 1);
		wake_up(&(dev_data)->wq);
		break;
	}
	case START_LOOPBACK: {
		if (copy_from_user(&(file_data)->device_info, (struct virtio_device_info_struct *) arg,
						   sizeof(struct virtio_device_info_struct)))
			return -EFAULT;

		/* Read and increase that value atomically */
		curr_avail_dev_id = atomic_add_return(1, &loopback_devices.device_num) - 1;

		/* Register a new loopback device */
		err = register_virtio_loopback_dev(curr_avail_dev_id);
		if (err)
			return -EFAULT;

		/* Wait for probe function to be called before return control to user-space app */
		wait_for_completion(&loopback_devices.reg_vl_dev_completion[curr_avail_dev_id]);

		/* Start the loopback */
		err = start_loopback(file_data, curr_avail_dev_id);
		if (err)
			return -EFAULT;

		break;
	}
	case IRQ:
		if (copy_from_user(&irq, (int *) arg, sizeof(int)))
			return -EFAULT;
		pr_debug("\nIRQ\n");
		/*
		 * Both of the interrupt ways work but a) is more stable
		 * and b) has better performance:
		 * a) vl_interrupt(NULL);
		 * b) queue_work(interrupt_workqueue, &async_interrupt);
		 */
		/* Call the function */
		vl_interrupt(file_data->vl_dev_irq, irq);
		break;
	case SHARE_VQS:
		if (copy_from_user(&queue_sel, (uint32_t *) arg, sizeof(uint32_t)))
			return -EFAULT;
		pr_debug("\n\nSHARE_VQS: %u\n\n", queue_sel);
		dev_data->vq_data.vq_pfn = dev_data->vq_data.vq_pfns[queue_sel];
		pr_debug("Selected pfn is: 0x%llx", dev_data->vq_data.vq_pfn);
		mm_data->share_vqs = true;
		break;
	case SHARE_COM_STRUCT:
		mm_data->share_communication_struct = true;
		break;
	default:
		pr_err("Unknown loopback ioctl: %u\n", cmd);
		return -ENOTTY;
	}

	return 0;
}

static int loopback_open(struct inode *inode, struct file *file)
{
	uint32_t val_1gb = 1024 * 1024 * 1024; // 1GB
	struct virtio_neg device_neg = {.done = ATOMIC_INIT(0)};
	/* Allocate file private data */
	struct file_priv_data *file_data = kmalloc(sizeof(struct file_priv_data),
							GFP_KERNEL);
	struct device_data *dev_data = kmalloc(sizeof(struct device_data), GFP_KERNEL);
	struct mmap_data *mm_data = kmalloc(sizeof(struct mmap_data), GFP_KERNEL);

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

static int loopback_release(struct inode *inode, struct file *file)
{
	struct file_priv_data *file_data = (struct file_priv_data *)(file->private_data);
	struct device_data *dev_data = (struct device_data *)file_data->dev_data;
	struct mmap_data *mm_data = (struct mmap_data *)file_data->mm_data;

	pr_info("Release the device\n");
	/*
	 * This makes the read/write do not wait
	 * for the virtio-loopback-adapter if
	 * the last has closed the fd
	 */
	dev_data->valid_eventfd = false;
	/* Active entry found */
	if (file_data->vl_dev_irq) {
		pr_debug("About to cancel the work\n");
		/* Cancel any pending work */
		cancel_work_sync(&file_data->vl_dev_irq->notify_work);
		/* Continue with the vl_dev unregister */
		virtio_loopback_remove(file_data->vl_dev_irq->pdev);
		file_data->vl_dev_irq = NULL;
	}
	/* Subsequently free the dev_data */
	free_page((unsigned long)dev_data->info->data);
	kfree(dev_data->info);
	eventfd_ctx_put(dev_data->efd_ctx);
	dev_data->efd_ctx = NULL;
	kfree(dev_data);
	file_data->dev_data = NULL;
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
	.unlocked_ioctl = loopback_ioctl,
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
	device_create(loopback_data.class, NULL, MKDEV(loopback_data.dev_major, 0),
						 NULL, "loopback");
	/* Create the workqueues of the loopback driver */
	loopback_data.notify_workqueue = create_singlethread_workqueue("notify_workqueue");

	/* Register virtio_loopback_transport */
	(void)platform_driver_register(&virtio_loopback_driver);

	/* Init loopback device array */
	atomic_set(&loopback_devices.device_num, 1);

	/* Init completion for all devices */
	for (i = 0; i < MAX_PDEV; i++)
		init_completion(&loopback_devices.reg_vl_dev_completion[i]);

	return 0;
}

static void __exit loopback_exit(void)
{
	int i;
	uint32_t max_used_device_num = atomic_read(&loopback_devices.device_num);

	pr_info("Exit driver!\n");

	/* Unregister loopback device */
	for (i = 0; i < max_used_device_num; i++)
		if (loopback_devices.devices[i])
			platform_device_unregister(loopback_devices.devices[i]->pdev);

	/* Unregister virtio_loopback_transport */
	platform_driver_unregister(&virtio_loopback_driver);
	pr_debug("platform_driver_unregister!\n");

	/* Necessary actions for the loopback_data */
	device_destroy(loopback_data.class, MKDEV(loopback_data.dev_major, 0));
	cdev_del(&loopback_data.cdev);
	pr_debug("device_destroy!\n");
	class_destroy(loopback_data.class);
	pr_debug("class_destroy!\n");

	/* Destroy the notify workqueue */
	flush_workqueue(loopback_data.notify_workqueue);
	destroy_workqueue(loopback_data.notify_workqueue);
}

module_init(loopback_init);
module_exit(loopback_exit);
