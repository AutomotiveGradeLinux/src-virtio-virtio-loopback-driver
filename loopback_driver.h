// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based on virtio_mmio.c
 *   Copyright 2011-2014, ARM Ltd.
 *
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

#define DRIVER	"LOOPBACK"

/* max Minor devices */
#define MAX_DEV	1

/* Define mmap elements limit */
#define MMAP_LIMIT	200

#ifdef DEBUG
#define DBG(...) pr_crit(__VA_ARGS__)
#else
#define DBG(...)
#endif /* DEBUG */

/*
 * The alignment to use between consumer and producer parts of vring.
 * Currently hardcoded to the page size.
 */
#define VIRTIO_MMIO_VRING_ALIGN		 PAGE_SIZE

#define to_virtio_mmio_device(_plat_dev) \
	container_of(_plat_dev, struct virtio_mmio_device, vdev)

typedef struct virtio_device_info_struct {
	unsigned long magic;
	unsigned long version;
	unsigned long device_id;
	unsigned long vendor;
} virtio_device_info_struct_t;

typedef struct virtio_neg {
	uint64_t notification;
	uint64_t data;
	uint64_t size;
	bool read;
	atomic_t done;
} virtio_neg_t;

struct virtio_mmio_device {
	struct virtio_device vdev;
	struct platform_device *pdev;

	void __iomem *base;
	unsigned long version;

	/* A list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
};

struct virtio_mmio_vq_info {
	/* the actual virtqueue */
	struct virtqueue *vq;

	/* the list node for the virtqueues list */
	struct list_head node;
};


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
 *	.name = a003e00.virtio_mmio
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

/* mmap finctionality */
#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

/* Define a bit for atomic test&set */
#define IN_USE_BIT 0

struct mmap_info {
	void *data;
	int reference;
};

/* Define a structure for your notify_list */
struct notify_data {
    uint32_t index;
    struct list_head list;
};

/* This stuct is used to share the eventfds between driver and userspace */
typedef struct efd_data {
	int efd[2];
	int pid;
} efd_data_t;

/* mmap functionality related structures */
struct share_mmap {
    uint64_t pfn;
    uint64_t vm_start;
    uint32_t size;
    uint32_t uid;
    struct page *page;
};

/* Mmap help funcitons */
/*
 * This functions registers all mmap calls done by the user-space into an array
 */
void add_share_mmap(struct file *filp, uint64_t pfn, uint64_t vm_start,
					uint64_t size, struct share_mmap *share_mmap_list, int *mmap_index)
{
    DBG("Add new mmaping! index: %d\n", *mmap_index);
    DBG("pfn: 0x%llx", pfn);
    DBG("vm_start: 0x%llx", vm_start);
    DBG("size: 0x%llx", size);

    share_mmap_list[*mmap_index].pfn = pfn;
    share_mmap_list[*mmap_index].vm_start = vm_start;
    share_mmap_list[*mmap_index].size = size;
    share_mmap_list[*mmap_index].uid = task_pid_nr(current);
    (*mmap_index)++;
}

/*
 * This functions removes a record from mmap array
 */
void share_mmap_rem(struct vm_area_struct *vma, struct share_mmap *share_mmap_list)
{
    int i;

    for (i = 0; i < MMAP_LIMIT; i++) {
        if (share_mmap_list[i].vm_start == vma->vm_start) {
            DBG("share_mmap with pa: 0x%llx and size: %x is deleted from the list\n",
                     share_mmap_list[i].pfn, share_mmap_list[i].size);
            share_mmap_list[i].uid = 0;
            share_mmap_list[i].pfn = 0;
            share_mmap_list[i].vm_start = 0;
            share_mmap_list[i].size = 0;
        }
    }
}

void print_mmap_idx(int i, struct share_mmap *share_mmap_list)
{
    DBG("share_mmap_list[%d].uid %x\n", i, share_mmap_list[i].uid);
    DBG("share_mmap_list[%d].pfn %llx\n", i, share_mmap_list[i].pfn);
    DBG("share_mmap_list[%d].vm_start %llx\n", i, share_mmap_list[i].vm_start);
    DBG("share_mmap_list[%d].size %x\n", i, share_mmap_list[i].size);
}


void print_mmaps(struct share_mmap *share_mmap_list, int mmap_index)
{
    int i;
    int limit = mmap_index == 0 ? MMAP_LIMIT : mmap_index;

    for (i = 0; i < limit; i++)
        print_mmap_idx(i, share_mmap_list);
}

/*
 * This function return the corresponding pfn of a user-space address
 * based on the mapping done during the initialization
 */
uint64_t share_mmap_exist_vma_return_correct_addr(uint64_t pfn, struct share_mmap *share_mmap_list)
{
    int i;
    uint64_t corrected_addr;

    for (i = 0; i < MMAP_LIMIT; i++) {
        if ((share_mmap_list[i].pfn <= pfn) &&
            (pfn < share_mmap_list[i].pfn + (share_mmap_list[i].size >> PAGE_SHIFT)) &&
            (share_mmap_list[i].uid == task_pid_nr(current))) {
            DBG("pfn (0x%llx) exist in: 0x%llx - 0x%llx\n", pfn, share_mmap_list[i].pfn,
                share_mmap_list[i].pfn + (share_mmap_list[i].size >> PAGE_SHIFT));
            corrected_addr = ((pfn - share_mmap_list[i].pfn) << PAGE_SHIFT) + share_mmap_list[i].vm_start;
            DBG("The return addr is: 0x%llx\n", corrected_addr);
            return corrected_addr;
        }
    }
    return 0;
}
/*
 * This function return the corresponding user-space address of a pfn
 * based on the mapping done during the initialization
 */
uint64_t share_mmap_exist_vma_return_correct_pfn(uint64_t addr, struct share_mmap *share_mmap_list)
{
    int i;
    uint64_t corrected_pfn;

    for (i = 0; i < MMAP_LIMIT; i++) {
        if ((share_mmap_list[i].vm_start <= addr) &&
            (addr < share_mmap_list[i].vm_start + share_mmap_list[i].size)) {
            DBG("addr (0x%llx) exist in: 0x%llx - 0x%llx\n", addr, share_mmap_list[i].vm_start,
                share_mmap_list[i].vm_start + share_mmap_list[i].size);
            DBG("((addr - share_mmap_list[i].vm_start) / PAGE_SIZE): 0x%llx\n",
                                            ((addr - share_mmap_list[i].vm_start) / PAGE_SIZE));
            DBG("share_mmap_list[i].pfn: 0x%llx\n", share_mmap_list[i].pfn);
            corrected_pfn = ((addr - share_mmap_list[i].vm_start) / PAGE_SIZE) + share_mmap_list[i].pfn;
            return corrected_pfn;
        }
    }
    return 0;
}

/*
 * This function returns the size of memory block area referrenced by the vrings
 */
uint64_t share_mmap_exist_vma_vring_size(uint64_t insert_pfn, struct vring *global_vring)
{
    int i = 0;
    uint64_t next_pfn, mem_blk_size;

    while (((vring_desc_t)global_vring->desc[i]).addr != 0) {

        /* Get the current value of pfn and its size */
        next_pfn = ((vring_desc_t)global_vring->desc[i]).addr >> PAGE_SHIFT;
        mem_blk_size = ((vring_desc_t)global_vring->desc[i]).len;

        /* Check if the insert_pfn is found */
        if (insert_pfn == next_pfn) {

            DBG("Found 0x%llx into the vring\n", insert_pfn);
            /* Formalize the mem_blk_size to be multiple of PAGE_SIZE */
            mem_blk_size = mem_blk_size % PAGE_SIZE ?
                    (mem_blk_size & PAGE_MASK) + PAGE_SIZE : mem_blk_size;
            DBG("The formalized size is %llu\n", mem_blk_size);

            return mem_blk_size;
        }

        /* Go to next element into the vring array */
        i++;
    }

    return PAGE_SIZE;
}

/*
 * This function tries to insert multiple PFNs into the user-space process.
 * The pfn of the starting page is given as an argument and the number of
 * pages to be inserted is calculated based on the memory block length found into
 * the vrings.
 */
void vmf_insert_vring_pfns(struct vm_area_struct *vma, uint64_t vaddr,
						   uint64_t insert_pfn, struct vring *global_vring)
{
    int i, page_num, ret;
    uint64_t mem_blk_size;

    /* Formalize the mem_blk_size to be multiple of PAGE_SIZE */
    mem_blk_size = share_mmap_exist_vma_vring_size(insert_pfn, global_vring);

    page_num = mem_blk_size / PAGE_SIZE;
    DBG("page_num: %u, need to be inserted\n", page_num);

    for (i = 0; i < page_num; i++) {
        DBG("\tTry to insert 0x%llx pfn into vaddr: 0x%llx with size of 0x%llx\n", insert_pfn, vaddr, mem_blk_size);
        if (!pfn_valid(insert_pfn))
            break;

        ret = vmf_insert_pfn(vma, vaddr, insert_pfn);
        DBG("vmf_insert_pfn returns: 0x%x\n", ret);

        /* Go to the next page of the memory block */
        vaddr += PAGE_SIZE;
        insert_pfn++;
    }
}

int mmap_mix(struct file *filp, struct vm_area_struct *vma,
			 struct share_mmap *share_mmap_list, int *mmap_index,
			 uint64_t vq_pfn)
{
    int ret = 0;
    unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);

    pr_crit("mmap mixx");

    ret = remap_pfn_range(vma, vma->vm_start, vq_pfn, size, vma->vm_page_prot);
    if (ret != 0) {
        DBG("Mmap error\n");
        print_mmaps(share_mmap_list, *mmap_index);
        goto out;
    }

    add_share_mmap(filp, vq_pfn, vma->vm_start, size, share_mmap_list, mmap_index);

out:
    return ret;
}

/* This funciton shares the communication struct with the userspace */
int mmap_communication_shared_space(struct file *filp, struct vm_area_struct *vma,
									struct share_mmap *share_mmap_list, int *mmap_index)
{
    unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
    struct mmap_info *com_mmap_virt = ((struct mmap_info *)(filp->private_data))->data;
    uint64_t com_mmap_pfn = ((uint64_t)virt_to_phys(com_mmap_virt)) >> PAGE_SHIFT;
    int ret;

    vm_flags_set(vma, VM_RESERVED);
    ret = remap_pfn_range(vma, vma->vm_start, com_mmap_pfn, size, vma->vm_page_prot);

    if (ret != 0) {
        DBG("Error to mmap communication shared space\n");
		goto out;
	}

    add_share_mmap(filp, com_mmap_pfn, vma->vm_start, size, share_mmap_list, mmap_index);

out:
    return ret;
}

/* A debug log function to help track the execution */
void print_neg_flag(uint64_t neg_flag, bool read)
{
	if (read)
		DBG("Read:\n");
	else
		DBG("Write:\n");

	switch (neg_flag) {
	case VIRTIO_MMIO_MAGIC_VALUE:		//0x000
		DBG("\tVIRTIO_MMIO_MAGIC_VALUE\n");
		break;
	case VIRTIO_MMIO_VERSION:		//0x004
		DBG("\tVIRTIO_MMIO_VERSION\n");
		break;
	case VIRTIO_MMIO_DEVICE_ID:		//0x008
		DBG("\tVIRTIO_MMIO_DEVICE_ID\n");
		break;
	case VIRTIO_MMIO_VENDOR_ID:		//0x00c
		DBG("\tVIRTIO_MMIO_VENDOR_ID\n");
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:	//0x010
		DBG("\tVIRTIO_MMIO_DEVICE_FEATURES\n");
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:	//0x014
		DBG("\tVIRTIO_MMIO_DEVICE_FEATURES_SEL\n");
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:	//0x020
		DBG("\tVIRTIO_MMIO_DRIVER_FEATURES\n");
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:	//0x024
		DBG("\tVIRTIO_MMIO_DRIVER_FEATURES_SEL\n");
		break;
	case VIRTIO_MMIO_GUEST_PAGE_SIZE:	//0x028
		DBG("\tVIRTIO_MMIO_GUEST_PAGE_SIZE\n");
		break;
	case VIRTIO_MMIO_QUEUE_SEL:		//0x030
		DBG("\tVIRTIO_MMIO_QUEUE_SEL\n");
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:		//0x034
		DBG("\tVIRTIO_MMIO_QUEUE_NUM_MAX\n");
		break;
	case VIRTIO_MMIO_QUEUE_NUM:		//0x038
		DBG("\tVIRTIO_MMIO_QUEUE_NUM\n");
		break;
	case VIRTIO_MMIO_QUEUE_ALIGN:		//0x03c
		DBG("\tVIRTIO_MMIO_QUEUE_ALIGN\n");
		break;
	case VIRTIO_MMIO_QUEUE_PFN:		//0x040
		DBG("\tVIRTIO_MMIO_QUEUE_PFN\n");
		break;
	case VIRTIO_MMIO_QUEUE_READY:		//0x044
		DBG("\tVIRTIO_MMIO_QUEUE_READY\n");
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:		//0x050
		DBG("\tVIRTIO_MMIO_QUEUE_NOTIFY\n");
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:	//0x060
		DBG("\tVIRTIO_MMIO_INTERRUPT_STATUS\n");
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:		//0x064
		DBG("\tVIRTIO_MMIO_INTERRUPT_ACK\n");
		break;
	case VIRTIO_MMIO_STATUS:		//0x070
		DBG("\tVIRTIO_MMIO_STATUS\n");
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:	//0x080
		DBG("\tVIRTIO_MMIO_QUEUE_DESC_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:	//0x084
		DBG("\tVIRTIO_MMIO_QUEUE_DESC_HIGH\n");
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:	//0x090
		DBG("\tVIRTIO_MMIO_QUEUE_AVAIL_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:	//0x094
		DBG("\tVIRTIO_MMIO_QUEUE_AVAIL_HIGH\n");
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:	//0x0a0
		DBG("\tVIRTIO_MMIO_QUEUE_USED_LOW\n");
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:	//0x0a4
		DBG("\tVIRTIO_MMIO_QUEUE_USED_HIGH\n");
		break;
	case VIRTIO_MMIO_SHM_SEL:		//0x0ac
		DBG("\tVIRTIO_MMIO_SHM_SEL\n");
		break;
	case VIRTIO_MMIO_SHM_LEN_LOW:		//0x0b0
		DBG("\tVIRTIO_MMIO_SHM_LEN_LOW\n");
		break;
	case VIRTIO_MMIO_SHM_LEN_HIGH:		//0x0b4
		DBG("\tVIRTIO_MMIO_SHM_LEN_HIGH\n");
		break;
	case VIRTIO_MMIO_SHM_BASE_LOW:		//0x0b8
		DBG("\tVIRTIO_MMIO_SHM_BASE_LOW\n");
		break;
	case VIRTIO_MMIO_SHM_BASE_HIGH:		//0x0bc
		DBG("\tVIRTIO_MMIO_SHM_BASE_HIGH\n");
		break;
	case VIRTIO_MMIO_CONFIG_GENERATION:	//0x0fc
		DBG("\tVIRTIO_MMIO_CONFIG_GENERATION\n");
		break;
	default:
		if (neg_flag >= VIRTIO_MMIO_CONFIG)
			DBG("\tVIRTIO_MMIO_CONFIG\n");
		else
			DBG("\tNegotiation flag Unknown: %lld\n", neg_flag);
		return;
	}
}

void print_data(const void *buf, size_t size)
{
	int offset = 10;
	int i, j;

	DBG("Print data from linux virtio-rng side:\n");

	printk(KERN_CRIT "");

	for (i = 0; i < size; i += offset) {
		printk(KERN_CONT "\t\t");

		for (j = i; (j < i + offset) && (j < size); j++)
			printk(KERN_CONT "%d, ", *((uint8_t *)(buf + j)));

		printk(KERN_CRIT "");
	}
}

/* IOCTL defines */
#define EFD_INIT _IOC(_IOC_WRITE, 'k', 1, sizeof(efd_data))
#define WAKEUP _IOC(_IOC_WRITE, 'k', 2, 0)
#define START_LOOPBACK _IOC(_IOC_WRITE, 'k', 3, sizeof(virtio_device_info_struct_t))
#define IRQ _IOC(_IOC_WRITE, 'k', 4, sizeof(int))
#define SHARE_VQS _IOC(_IOC_WRITE, 'k', 5, sizeof(uint32_t))
#define SHARE_BUF _IOC(_IOC_WRITE, 'k', 6, sizeof(uint64_t))
#define SHARE_COM_STRUCT _IOC(_IOC_WRITE, 'k', 7, 0)

/* device data holder, this structure may be extended to hold additional data */
struct loopback_device_data {
	struct cdev cdev;
};

#endif /* __VIRTUALNET_H__ */
