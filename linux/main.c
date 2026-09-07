/******************************************************************************
 *                                                                             *
 * License Agreement                                                           *
 *                                                                             *
 * Copyright (c) 2026 Elsys AG, Niederrohrdorf, Switzerland                    *
 * All rights reserved.                                                        *
 *                                                                             *
 * Permission is hereby granted, free of charge, to any person obtaining a     *
 * copy of this software and associated documentation files (the "Software"),  *
 * to deal in the Software without restriction, including without limitation   *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,    *
 * and/or sell copies of the Software, and to permit persons to whom the       *
 * Software is furnished to do so, subject to the following conditions:        *
 *                                                                             *
 * The above copyright notice and this permission notice shall be included in  *
 * all copies or substantial portions of the Software.                         *
 *                                                                             *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *
 * DEALINGS IN THE SOFTWARE.                                                   *
 *                                                                             *
 * This agreement shall be governed in all respects by the laws of             *
 * Switzerland.                                                                *
 *                                                                             *
 ******************************************************************************/

#include "altera_msgdma.h"
#include "dma.h"
#include "request.h"
#include "tpce.h"
#include "tpce_public.h"

#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#define TPCE_MODULE_VERSION "1.2.0"
MODULE_VERSION(TPCE_MODULE_VERSION);
MODULE_AUTHOR("roman.bertschi@elsys.ch");
MODULE_AUTHOR("philipp.dasen@elsys.ch");
MODULE_DESCRIPTION("TPCE Gen2 DAQ Device Driver");
MODULE_LICENSE("Dual MIT/GPL");

//=====================================================================
// Enhancement ideas:
// 1) Add eventfd field to ioctl_data structure and IOC_GET_ASYNC_RESULT ioctl to implement WDF-style async I/O
//=====================================================================

//=====================================================================
// Sysfs Attributes
//=====================================================================

static ssize_t driver_version_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, TPCE_MODULE_VERSION "\n");
}
static DEVICE_ATTR_RO(driver_version);

//=====================================================================
// Global device information
//=====================================================================

typedef struct {
    atomic_t count;
    tpce_dev *devs[TPCE_MAX_BOARDS];
} tpce_dev_table;

static tpce_dev_table tpce_devices;

static dev_t first;       // Global variable for the first device number
static struct class *cl;  // Global variable for the device class

static DEFINE_IDA(tpce_minor_ida);

static int find_free_minor(void) {
    return ida_alloc_range(&tpce_minor_ida, 0, TPCE_MAX_BOARDS - 1, GFP_KERNEL);
}

static void release_minor(int minor) {
    ida_free(&tpce_minor_ida, minor);
}

//=====================================================================
// Open the device
//=====================================================================

static int tpce_open(struct inode *inode, struct file *file) {
    int minor = MINOR(inode->i_rdev);
    int major = MAJOR(inode->i_rdev);

    if (minor >= TPCE_MAX_BOARDS || tpce_devices.devs[minor] == NULL) {
        pr_crit("Attempted to open device file but device data has not been initialized\n");
        return -ENODEV;
    }
    tpce_dev *tpce = tpce_devices.devs[minor];

    if (READ_ONCE(tpce->going_away)) {
        return -ENODEV;
    }

    // Initialize device file context
    struct tpce_file_ctx *file_ctx = kzalloc(sizeof(struct tpce_file_ctx), GFP_KERNEL);
    if (!file_ctx) {
        return -ENOMEM;
    }
    INIT_LIST_HEAD(&file_ctx->dma_map_sublist);
    file_ctx->tpce = tpce;

    refcount_inc(&tpce->refs);
    file->private_data = file_ctx;

    dev_dbg(tpce->dev, "tpce_open with major %i and minor %d\n", major, minor);
    return 0;
}

//=====================================================================
// Close the device
//=====================================================================

static int tpce_release(struct inode *inode, struct file *file) {
    struct tpce_file_ctx *file_ctx = file->private_data;
    struct tpce_dev *tpce          = file_ctx->tpce;

    mutex_lock(&tpce->mutex);
    tpce_dma_map_info *entry;
    tpce_dma_map_info *temp;
    // Close all remaining DMA-mappings
    list_for_each_entry_safe(entry, temp, &file_ctx->dma_map_sublist, file_ctx_node) {
        WARN_ONCE(entry->owner != file_ctx, "Wrong DMA mapping owner during sublist removal");
        list_del(&entry->file_ctx_node);
        tpce_safe_dma_unmap(tpce, entry);  // Also stops active DMA transfers before unmap
    }
    mutex_unlock(&tpce->mutex);

    if (refcount_dec_and_test(&tpce->refs)) {
        kfree(tpce);
    }

    file->private_data = NULL;
    return 0;
}

//=====================================================================
// Ioctl handler
//=====================================================================

static long tpce_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct tpce_file_ctx *file_ctx = file->private_data;
    struct tpce_dev *tpce          = file_ctx->tpce;
    internal_ioctl_data iodata_drv;
    ioctl_data iodata_usr;

    bool localbuffer = false;

    if (copy_from_user(&iodata_usr, (void __user *)arg, sizeof(ioctl_data))) {
        return -EFAULT;
    }

    dev_dbg_ratelimited(tpce->dev, "ioctl: cmd=%s (0x%08x) in=%p size=%zu out=%p size=%zu\n", tpce_ioctl_name(cmd), cmd,
                        iodata_usr.in_buffer, iodata_usr.in_size, iodata_usr.out_buffer, iodata_usr.out_size);

    // IOC_EVENT ioctl does not use the regular command dispatcher mechanism
    switch (cmd) {
        case TPCE_IOC_EVENT: {
            spin_lock_bh(&tpce->hw_events.events_lock);
            u32 events             = tpce->hw_events.status;
            tpce->hw_events.status = 0;
            spin_unlock_bh(&tpce->hw_events.events_lock);

            if (copy_to_user((void __user *)iodata_usr.out_buffer, (void *)&events, sizeof(events))) {
                dev_dbg(tpce->dev, "Error copying hardware event data to user, some events may have been lost\n");
                return -EFAULT;
            }
            return 0;
        }
        case TPCE_IOC_DMA_STATUS: {
            if (copy_from_user(tpce->io_buffer, (void __user *)iodata_usr.in_buffer, iodata_usr.in_size)) {
                return -EFAULT;
            }
            u32 channel = *(u32 *)tpce->io_buffer;
            if (channel >= TPCE_DMA_NUM_CHANNELS) return -EINVAL;
            s32 status = tpce_dma_state_locked(tpce, &tpce->dma_info[channel]);

            if (copy_to_user((void __user *)iodata_usr.out_buffer, (void *)&status, sizeof(status))) {
                return -EFAULT;
            }
            return 0;
        }
    }

    if (mutex_lock_killable(&tpce->mutex)) {
        return -EINTR;  // User process has been interrupted by fatal signal
    }

    long status = 0;
    if (tpce->going_away) {
        status = -ENODEV;  // Device is being removed
        goto cleanup;
    }

    // Prepare IO buffer data structure
    iodata_drv.file_ctx = file_ctx;
    iodata_drv.size     = max_t(size_t, iodata_usr.in_size, iodata_usr.out_size);

    if (iodata_drv.size > TPCE_IOBUFFERSIZE) {
        // alloc a local buffer
        iodata_drv.io_buffer = kvzalloc(iodata_drv.size, GFP_KERNEL);
        if (!iodata_drv.io_buffer) {
            dev_dbg(tpce->dev, "Failed to allocate io buffer\n");
            status = -ENOMEM;
            goto cleanup;
        }
        localbuffer = true;
    }
    else {
        iodata_drv.io_buffer = tpce->io_buffer;
    }

    // Copy input data from userspace
    if (iodata_usr.in_size > 0) {
        if (copy_from_user(iodata_drv.io_buffer, (void __user *)iodata_usr.in_buffer, iodata_usr.in_size)) {
            dev_dbg(tpce->dev, "Bad parameters in tpce_ioctl, copy_from_user: from %p to %px Size %zu\n",
                    iodata_usr.in_buffer, (void __user *)iodata_drv.io_buffer, iodata_usr.out_size);
            status = -EFAULT;
            goto cleanup;
        }
    }

    status = (long)tpce_dispatch_ioc(tpce, cmd, &iodata_drv);

    // Copy output data to userspace
    if (!status && iodata_usr.out_size > 0) {
        if (copy_to_user((void __user *)iodata_usr.out_buffer, iodata_drv.io_buffer, iodata_usr.out_size)) {
            dev_dbg(tpce->dev, "Bad parameters in tpce_ioctl, copy_to_user: to %px from %p Size %zu\n",
                    (void __user *)iodata_usr.out_buffer, iodata_drv.io_buffer, iodata_usr.out_size);
            status = -EFAULT;
            goto cleanup;
        }
    }

    tpce_dev_dbg_ratelimited(tpce->dev, "ioctl: cmd=%s completed with status=%ld\n", tpce_ioctl_name(cmd), status);
cleanup:
    mutex_unlock(&tpce->mutex);
    if (localbuffer) {
        kvfree(iodata_drv.io_buffer);
    }
    return status;
}

//=====================================================================
// 32-bit user process ioctl handler
//=====================================================================

static long tpce_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    return -ENOTTY;  // 32-bit userland on 64-bit kernel is not supported
}

//=====================================================================
// Wait on select events
//=====================================================================

static __poll_t tpce_poll(struct file *file, struct poll_table_struct *wait) {
    struct tpce_file_ctx *file_ctx = (struct tpce_file_ctx *)file->private_data;

    __poll_t ret   = 0;
    tpce_dev *tpce = file_ctx->tpce;

    // Always register the wait queue first. This never sleeps on the second poll pass (wait == NULL), and must not be
    // under a spinlock on the first pass because it may allocate.
    poll_wait(file, &tpce->hw_events.wq, wait);

    spin_lock_bh(&tpce->hw_events.events_lock);
    if (tpce->hw_events.pending) {
        tpce_dev_dbg_ratelimited(tpce->dev, "tpce_poll pending event raised\n");
        tpce->hw_events.pending = false;
        ret                     = POLLIN | POLLRDNORM;
    }
    spin_unlock_bh(&tpce->hw_events.events_lock);

    return ret;
}

//=====================================================================
// Mmap() support
//=====================================================================

static void vma_stream_info_open(struct vm_area_struct *vma) {
    internal_dma_stream_info *info = vma->vm_private_data;
    refcount_inc(&info->state_refs);  // Mutex is not needed here
}

static void vma_stream_info_close(struct vm_area_struct *vma) {
    internal_dma_stream_info *info = vma->vm_private_data;
    tpce_dma_info *dma_info        = container_of(info, tpce_dma_info, stream_info);

    mutex_lock(&dma_info->tpce->mutex);
    tpce_free_stream_info(info);
    mutex_unlock(&dma_info->tpce->mutex);
}

static const struct vm_operations_struct stream_info_vm_ops = {
  .open  = vma_stream_info_open,
  .close = vma_stream_info_close,
};

static int tpce_mmap(struct file *file, struct vm_area_struct *vma) {
    int err;
    unsigned long off  = vma->vm_pgoff;
    unsigned long size = vma->vm_end - vma->vm_start;
    tpce_dev *tpce     = ((struct tpce_file_ctx *)file->private_data)->tpce;

    dev_dbg(tpce->dev, "mmap at offset: %lx", off);
    switch (off) {
        case TPCE_MMAP_BAR0_OFF:
        case TPCE_MMAP_BAR2_OFF:        return -ENOSYS;
        case TPCE_MMAP_DMA0_STREAM_OFF:
        case TPCE_MMAP_DMA1_STREAM_OFF:
        case TPCE_MMAP_DMA2_STREAM_OFF: {
            unsigned long channel = off & 0xf;

            if (size != PAGE_SIZE) return -EINVAL;
            if (channel >= TPCE_DMA_NUM_CHANNELS) return -EINVAL;

            err = mutex_lock_killable(&tpce->mutex);
            if (err) return err;
            if (tpce->going_away) {
                return -ENODEV;
            }

            internal_dma_stream_info *info = &tpce->dma_info[channel].stream_info;
            if (!info->state) {
                err = tpce_init_stream_info(info);
                if (err) return err;
            }
            else {
                refcount_inc(&info->state_refs);
            }
            mutex_unlock(&tpce->mutex);

            vma->vm_private_data = info;
            vma->vm_ops          = &stream_info_vm_ops;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
            vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
#else
            vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#endif
            err = remap_vmalloc_range(vma, tpce->dma_info[channel].stream_info.state, 0);
            if (err) {
                goto error_remap;
            }
            return 0;
        error_remap:
            tpce_free_stream_info(info);
            return err;
        }
        default: return -EINVAL;
    }
}

//=====================================================================
// Dma Workqueue Handler
//=====================================================================

static void tpce_dma_work(struct work_struct *data) {
    tpce_dma_info *dma_info = container_of(data, tpce_dma_info, work);

    mutex_lock(&dma_info->mutex);
    tpce_dma_completion(dma_info);
    mutex_unlock(&dma_info->mutex);

    wake_up(&dma_info->wq);
}

//=====================================================================
// Tasklet Handler
//=====================================================================

static void tpce_tasklet(unsigned long data) {
    tpce_dev *tpce = (tpce_dev *)data;
    unsigned long flags;

    spin_lock_irqsave(&tpce->irqsrc_lock, flags);
    u32 irqsrc   = tpce->irqsrc;
    tpce->irqsrc = 0;
    spin_unlock_irqrestore(&tpce->irqsrc_lock, flags);

    if (irqsrc & TPCE_IRQ_DMA0) {
        // Clear interrupt
        u32 value = ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET_MASK;
        u32 reg   = TPCE_DMA_PREFETCHER_CSR(0, ALT_MSGDMA_PREFETCHER_STATUS_REG);
        iowrite32(value, BAR2_MEMORY_MAP(tpce, reg));

        // Schedule DMA completion handler in process context
        queue_work(system_highpri_wq, &tpce->dma_info[0].work);
    }

    if (irqsrc & TPCE_IRQ_DMA1) {
        u32 value = ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET_MASK;
        u32 reg   = TPCE_DMA_PREFETCHER_CSR(1, ALT_MSGDMA_PREFETCHER_STATUS_REG);
        iowrite32(value, BAR2_MEMORY_MAP(tpce, reg));

        queue_work(system_highpri_wq, &tpce->dma_info[1].work);
    }

    if (irqsrc & TPCE_IRQ_DMA2) {
        u32 value = ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET_MASK;
        u32 reg   = TPCE_DMA_PREFETCHER_CSR(2, ALT_MSGDMA_PREFETCHER_STATUS_REG);
        iowrite32(value, BAR2_MEMORY_MAP(tpce, reg));

        queue_work(system_highpri_wq, &tpce->dma_info[2].work);
    }

    if (irqsrc & TPCE_IRQ_APP) {
        spin_lock(&tpce->hw_events.events_lock);
        tpce->hw_events.status |= ioread32(BAR0_MEMORY_MAP(tpce, TPCE_AVALON_OFFSET + TPCE_G2_INTSTAT));
        tpce->hw_events.pending = true;
        // Clear any application interrupts
        iowrite32(tpce->hw_events.status, BAR0_MEMORY_MAP(tpce, TPCE_AVALON_OFFSET + TPCE_G2_INTCLR));
        spin_unlock(&tpce->hw_events.events_lock);

        wake_up_interruptible(&tpce->hw_events.wq);
    }

    // Reenable interrupts
    if (!READ_ONCE(tpce->going_away)) {
        iowrite32(TPCE_IRQ_ENABLED, BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));
        ioread32(BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));
    }
}

//=====================================================================
// Interrupt Handler
//=====================================================================

// Only correct for DMA_FROM_DEVICE direction. Requires sequence numbers to be contiguous and zero-based: 0..n-1.
static __always_inline void dma_stream_irq_handler(tpce_dev *tpce, int channel) {
    // Clear interrupt
    u32 value = ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET_MASK;
    u32 reg   = TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_STATUS_REG);
    iowrite32(value, BAR2_MEMORY_MAP(tpce, reg));
    ioread32(BAR2_MEMORY_MAP(tpce, reg));

    tpce_dma_info *dma_info               = &tpce->dma_info[channel];
    internal_dma_stream_info *stream_info = &dma_info->stream_info;
    tpce_dma_stream_state *state          = stream_info->state;
    if (unlikely(!state)) {
        WARN_ONCE(1, "dma_info stream state is NULL");
        return;
    }

    // Read sequence number register
    reg          = TPCE_DMA_CSR(channel, ALTERA_MSGDMA_CSR_DESCRIPTOR_SEQUENCE_NUMBER_REG);
    u32 seq_nums = ioread32(BAR2_MEMORY_MAP(tpce, reg));

    u32 bytes_per_desc  = dma_info->params.bytes_per_descriptor;
    u32 num_descriptors = dma_info->desc_size / sizeof(tpce_dma_descriptor);
    u64 produced        = state->produced;
    u64 irq_cnt         = stream_info->irq_cnt;
    s32 write_seq_num   = stream_info->write_seq_num;
    u32 num_desc_mask   = stream_info->num_desc_mask;

    // clang-format off
    s32 new_write_seq_num = num_desc_mask ? (s32)((seq_nums >> 16) & num_desc_mask)
                                          : (s32)((seq_nums >> 16) % num_descriptors);
    // clang-format on
    s32 num_processed = (new_write_seq_num - write_seq_num);
    if (new_write_seq_num < write_seq_num) {
        // Handle wraparound
        num_processed += num_descriptors;
    }
    u64 new_produced = produced + (bytes_per_desc * num_processed);
    u64 new_irq_cnt  = irq_cnt + 1;

    if (unlikely(num_processed < 0 || num_processed > num_descriptors)) {
        dev_warn_ratelimited(tpce->dev, "Invalid DMA sequence advance: old=%d new=%d ndesc=%u processed=%d\n",
                             write_seq_num, new_write_seq_num, num_descriptors, num_processed);
        return;
    }

    s32 i = write_seq_num;
    // The actual loop uses i, but we also iterate over a bounded n, to guarantee that the ISR terminates even with
    // badly programmed seq_nums
    s32 n;
    for (n = 0; n < num_processed; ++n) {
        tpce_dma_sync_for_cpu_partial(dma_info, i);
        if (++i == num_descriptors) {
            i = 0;
        }
    }

    stream_info->write_seq_num = new_write_seq_num;
    stream_info->irq_cnt       = new_irq_cnt;
#if BITS_PER_LONG == 64
    smp_store_release(&state->produced, new_produced);
#else
    smp_wmb();
    WRITE_ONCE(state->produced, new_produced);
#endif

    tpce_dev_dbg_ratelimited(tpce->dev, "Stream IRQ handler: produced = %llu, desc_seq_num = %u, irq_cnt = %llu",
                             new_produced, new_write_seq_num, new_irq_cnt);
}

static irqreturn_t tpce_isr(int irq, void *dev_id) {
    tpce_dev *tpce = (tpce_dev *)dev_id;
    u32 irqreg, fast_mask, irqreg_fm;

    irqreg = ioread32(BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_STATUS_REG));

    // Return if no interrupts are active
    if (irqreg == 0) {
        return IRQ_NONE;
    }

    fast_mask = smp_load_acquire(&tpce->fast_irq_mask);
    irqreg_fm = irqreg & fast_mask;
    if (irqreg_fm) {
        if (irqreg_fm & TPCE_IRQ_DMA0) {
            dma_stream_irq_handler(tpce, 0);
        }

        if (irqreg_fm & TPCE_IRQ_DMA1) {
            dma_stream_irq_handler(tpce, 1);
        }

        if (irqreg_fm & TPCE_IRQ_DMA2) {
            dma_stream_irq_handler(tpce, 2);
        }

        irqreg &= ~fast_mask;
        if (irqreg == 0) {
            return IRQ_HANDLED;
        }
    }

    // Disable interrupt generation
    iowrite32(0x0, BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));
    ioread32(BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));

    // Update internal interrupt status variable for future retrieval
    spin_lock(&tpce->irqsrc_lock);
    tpce->irqsrc |= irqreg;
    spin_unlock(&tpce->irqsrc_lock);

    tasklet_hi_schedule(&tpce->tl_descr);
    return IRQ_HANDLED;
}

//=====================================================================
// File operations
//=====================================================================

static struct file_operations pci_fops = {
  .owner          = THIS_MODULE,
  .open           = tpce_open,
  .release        = tpce_release,
  .unlocked_ioctl = tpce_ioctl,
  .compat_ioctl   = tpce_compat_ioctl,
  .poll           = tpce_poll,
  .mmap           = tpce_mmap,
};

//=====================================================================
// PCI Resource Initialization
//=====================================================================

static int map_pci_bar(struct pci_dev *pdev, pci_bar_info *bar, int resource, int mempos) {
    if (mempos >= TPCE_NUM_BARS || resource >= DEVICE_COUNT_RESOURCE) {
        return -EINVAL;
    }
    bar[mempos].paddr = pci_resource_start(pdev, resource);
    bar[mempos].size  = pci_resource_len(pdev, resource);

    // Verify the resources are valid
    if (!bar[mempos].paddr || !bar[mempos].size) {
        dev_err(&pdev->dev, "Invalid BAR%d resources\n", resource);
        return -ENOMEM;
    }

    dev_dbg(&pdev->dev, "BAR%d: addr=%pa, size=%pa, flags=0x%lx\n", resource, &bar[mempos].paddr, &bar[mempos].size,
            pci_resource_flags(pdev, resource));

    bar[mempos].vaddr = ioremap(bar[mempos].paddr, bar[mempos].size);
    if (!bar[mempos].vaddr) {
        dev_err(&pdev->dev, "ioremap failed for BAR%d\n", resource);
        return -ENOMEM;
    }

    return 0;
}

static int tpce_pci_init(tpce_dev *tpce) {
    struct pci_dev *pdev = tpce->pcidev;
    pci_bar_info *bars   = tpce->pci_bars;

    int err  = 0;
    int nvec = 1;
    int irq;

    err = pci_enable_device(pdev);
    if (err) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        return err;
    }

    // Request all PCI regions
    err = pci_request_regions(pdev, _DRV_NAME_);
    if (err) {
        dev_err(&pdev->dev, "Failed to request PCI regions\n");
        goto error_regions;
    }

    // First memory-area (BAR0)
    err = map_pci_bar(pdev, bars, 0, 0);
    if (err) {
        goto error_bar0;
    }

    // Second memory-area (BAR2)
    err = map_pci_bar(pdev, bars, 2, 1);
    if (err) {
        goto error_bar2;
    }

    // Register interrupt handler
    nvec = pci_alloc_irq_vectors(pdev, 1, TPCE_MAX_MSI, PCI_IRQ_MSI);
    dev_dbg(&pdev->dev, "Nr of MSI Vectors allocated %d\n", nvec);
    if (nvec < 0) {
        dev_err(&pdev->dev, "Allocating IRQ Vectors failed\n");
        err = nvec;
        goto error_irq;
    }

    // Request IRQ for first vector only (or loop through all if needed)
    irq       = pci_irq_vector(pdev, 0);
    pdev->irq = irq;
    err       = request_irq(irq, tpce_isr, 0, _DRV_NAME_, tpce);
    if (err) {
        dev_err(&pdev->dev, "IRQ %d not free.\n", pdev->irq);
        goto error_request_irq;
    }

    // enable bus master capability
    pci_set_master(pdev);

    dev_dbg(&pdev->dev, "tpce_pci_init done\n");
    return 0;

// -- error handling --
error_request_irq:
    pci_free_irq_vectors(pdev);

error_irq:
    if (bars[1].vaddr) {
        iounmap((void *)bars[1].vaddr);
    }

error_bar2:
    if (bars[0].vaddr) {
        iounmap((void *)bars[0].vaddr);
    }

error_bar0:
    pci_release_regions(pdev);

error_regions:
    pci_disable_device(pdev);

    return err;
}

//=====================================================================
// PCI Resource Deallocation
//=====================================================================

static void tpce_pci_free(tpce_dev *tpce) {
    struct pci_dev *dev = tpce->pcidev;
    int i;

    free_irq(dev->irq, tpce);
    pci_free_irq_vectors(dev);

    for (i = 0; i < TPCE_NUM_BARS; i++) {
        if (tpce->pci_bars[i].vaddr) {
            iounmap((void *)tpce->pci_bars[i].vaddr);
        }
    }

    pci_release_regions(dev);
    pci_disable_device(dev);
}

//=====================================================================
// Device Initialization
//=====================================================================

static int tpce_device_init(struct pci_dev *pdev, const struct pci_device_id *id) {
    int minor;
    tpce_dev *tpce;

    int ret = 0;
    dev_dbg(&pdev->dev, "Initializing TPCE device\n");

    // Reserve memory for driver data
    tpce = kzalloc(sizeof(tpce_dev), GFP_KERNEL);
    if (tpce == NULL) {
        dev_err(&pdev->dev, "Error allocating tpce device data memory\n");
        ret = -ENOMEM;
        goto error_alloc1;
    }

    // Init IO Buffer
    tpce->io_buffer = kzalloc(TPCE_IOBUFFERSIZE, GFP_KERNEL);
    if (tpce->io_buffer == NULL) {
        dev_err(&pdev->dev, "Error allocating static IO-Buffer memory\n");
        ret = -ENOMEM;
        goto error_alloc2;
    }

    // Get a minor number
    minor = find_free_minor();
    if (minor < 0) {
        dev_err(&pdev->dev, "Failed to allocate minor number\n");
        goto error_ida;
    }

    tpce->minor              = minor;
    tpce->pcidev             = pdev;
    tpce_devices.devs[minor] = tpce;
    atomic_inc(&tpce_devices.count);
    pci_set_drvdata(pdev, tpce);
    refcount_set(&tpce->refs, 1);
    tpce->going_away = false;

    // Register interrupt tasklet
    tasklet_init(&tpce->tl_descr, (void *)tpce_tasklet, (unsigned long)tpce);

    // Initialize PCI functionality
    if ((ret = tpce_pci_init(tpce))) {
        dev_err(&pdev->dev, "Error in tpce_pci_init: %pe\n", ERR_PTR(ret));
        goto error_pci_init;
    }

    // Events
    init_waitqueue_head(&tpce->hw_events.wq);
    spin_lock_init(&tpce->hw_events.events_lock);
    tpce->hw_events.status  = 0;
    tpce->hw_events.pending = false;

    // Initialize dma_info members
    int i;
    for (i = 0; i < TPCE_DMA_NUM_CHANNELS; ++i) {
        tpce->dma_info[i].tpce = tpce;
        mutex_init(&tpce->dma_info[i].mutex);
        INIT_WORK(&tpce->dma_info[i].work, tpce_dma_work);
        init_waitqueue_head(&tpce->dma_info[i].wq);
        tpce->dma_info[i].state = TPCE_DMA_IDLE;
    }

    // Ioctl mutext
    mutex_init(&tpce->mutex);

    // Initialize DMA data structures
    if (tpce_dma_init(tpce)) {
        dev_err(&pdev->dev, "Error in tpce_dma_init: %pe\n", ERR_PTR(ret));
        goto error_dma_init;
    }

    // Initialize character device
    dev_t devnum = MKDEV(MAJOR(first), minor);
    cdev_init(&tpce->cdev, &pci_fops);
    cdev_set_parent(&tpce->cdev, &pdev->dev.kobj);  // Ensures that release occurs after cdev has been closed
    tpce->cdev.owner = THIS_MODULE;
    if ((ret = cdev_add(&tpce->cdev, devnum, 1))) {
        pr_err("Error adding character device: %pe\n", ERR_PTR(ret));
        goto error_cdev_add;
    }

    // Create device
    if (!(tpce->dev = device_create(cl, &pdev->dev, devnum, tpce, "tpce%d", minor))) {
        dev_err(&pdev->dev, "Error creating device: /dev/tpce%d.\n", minor);
        goto error_device_create;
    }

    if ((ret = device_create_file(tpce->dev, &dev_attr_driver_version))) {
        dev_warn(tpce->dev, "Failed to create sysfs attribute: %pe\n", ERR_PTR(ret));
    }

    dev_info(tpce->dev, " TPCE Device Initialized:\n");
    dev_info(tpce->dev, " PCI Slot:  %s\n", pci_name(pdev));
    dev_info(tpce->dev, " Vendor:    %04x  Device:    %04x\n", pdev->vendor, pdev->device);
    dev_info(tpce->dev, " Subvendor: %04x  Subdevice: %04x\n", pdev->subsystem_vendor, pdev->subsystem_device);
    dev_info(tpce->dev, " BAR0:      %pR\n", &pdev->resource[0]);
    dev_info(tpce->dev, " BAR2:      %pR\n", &pdev->resource[2]);

    return 0;

error_device_create:
    cdev_del(&tpce->cdev);
error_cdev_add:
    tpce_dma_free(tpce);
error_dma_init:
    tpce_pci_free(tpce);
error_pci_init:
    tasklet_kill(&tpce->tl_descr);
    pci_set_drvdata(pdev, NULL);
    tpce_devices.devs[minor] = NULL;
    atomic_dec(&tpce_devices.count);
    release_minor(minor);
error_ida:
    kfree(tpce->io_buffer);
error_alloc2:
    kfree(tpce);
error_alloc1:
    if (ret == 0) ret = -EIO;  // Set generic error code if err == 0
    return ret;
}

//=====================================================================
// Device Removal
//=====================================================================

static void tpce_device_remove(struct pci_dev *pdev) {
    int minor;
    tpce_dev *tpce;

    if (!(tpce = pci_get_drvdata(pdev))) {
        dev_err(&pdev->dev, "Unable to retrieve driver data for device removal\n");
        return;
    }
    minor = tpce->minor;

    // Set "going_away" flag to true and reset DMA hardware
    // TODO Reset other hardware too
    mutex_lock(&tpce->mutex);
    tpce->going_away = true;
    mutex_unlock(&tpce->mutex);

    device_remove_file(tpce->dev, &dev_attr_driver_version);
    device_destroy(cl, MKDEV(MAJOR(first), minor));
    cdev_del(&tpce->cdev);

    // Explicitly disable interrupt generation so bottom half does not get scheduled
    iowrite32(0x0, BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));
    ioread32(BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_IRQ_ENABLE_REG));
    synchronize_irq(tpce->pcidev->irq);

    tasklet_kill(&tpce->tl_descr);

    int i;
    for (i = 0; i < TPCE_DMA_NUM_CHANNELS; ++i) {
        cancel_work_sync(&tpce->dma_info[i].work);
    }

    mutex_lock(&tpce->mutex);
    for (i = 0; i < TPCE_DMA_NUM_CHANNELS; ++i) {
        reset_dma_info(tpce, &tpce->dma_info[i]);
    }
    mutex_unlock(&tpce->mutex);

    tpce_dma_free(tpce);
    tpce_pci_free(tpce);
    pci_set_drvdata(pdev, NULL);
    tpce_devices.devs[minor] = NULL;
    atomic_dec(&tpce_devices.count);
    release_minor(minor);
    kfree(tpce->io_buffer);

    if (refcount_dec_and_test(&tpce->refs)) {
        kfree(tpce);
    }

    dev_info(&pdev->dev, "Device Removed\n");
}

//=====================================================================
// Driver Parameters
//=====================================================================

#ifndef PCI_VENDOR_ID_ALTERA
#define PCI_VENDOR_ID_ALTERA 0x1172
#endif

#ifndef PCI_DEVICE_ID_TPCE
#define PCI_DEVICE_ID_TPCE 0xe001
#endif

// We specify individual subsystem IDs. Doing this gives our driver precedence over the generic altera_cvp driver
// clang-format off
static struct pci_device_id tpce_pci_ids[] = {
    { PCI_DEVICE_SUB(PCI_VENDOR_ID_ALTERA, PCI_DEVICE_ID_TPCE, PCI_VENDOR_ID_ALTERA, 0xe071) },
    { PCI_DEVICE_SUB(PCI_VENDOR_ID_ALTERA, PCI_DEVICE_ID_TPCE, PCI_VENDOR_ID_ALTERA, 0xe072) },
    { PCI_DEVICE_SUB(PCI_VENDOR_ID_ALTERA, PCI_DEVICE_ID_TPCE, PCI_VENDOR_ID_ALTERA, 0xe073) },
    { PCI_DEVICE_SUB(PCI_VENDOR_ID_ALTERA, PCI_DEVICE_ID_TPCE, PCI_VENDOR_ID_ALTERA, 0xe074) },
    { 0, }
};
// clang-format on

MODULE_DEVICE_TABLE(pci, tpce_pci_ids);

static struct pci_driver pci_drv = {
  .name     = _DRV_NAME_,
  .id_table = tpce_pci_ids,
  .probe    = tpce_device_init,
  .remove   = tpce_device_remove,
};

//=====================================================================
// Load driver
//=====================================================================

static int __init drv_init(void) {
    pr_debug("Entering drv_init\n");
    memset(&tpce_devices, 0, sizeof(tpce_devices));

    if (alloc_chrdev_region(&first, 0, TPCE_MAX_BOARDS, _DRV_NAME_)) {
        pr_err("alloc_chrdev_region failed\n");
        goto error_register;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    if (!(cl = class_create("daq"))) {
#else
    if (!(cl = class_create(THIS_MODULE, "daq"))) {
#endif
        pr_err("class_create failed\n");
        goto error_class_create;
    }

    if (pci_register_driver(&pci_drv)) {
        pr_err("pci_register_driver failed\n");
        goto error_pci_register;
    }

    pr_debug("Leaving drv_init\n");
    return 0;

error_pci_register:
    class_destroy(cl);
error_class_create:
    unregister_chrdev_region(first, TPCE_MAX_BOARDS);
error_register:
    return -EIO;
}

//=====================================================================
// Driver unload
//=====================================================================

static void __exit drv_exit(void) {
    pci_unregister_driver(&pci_drv);
    class_destroy(cl);
    unregister_chrdev_region(first, TPCE_MAX_BOARDS);
    pr_info("Driver Unloaded");
}

module_init(drv_init);
module_exit(drv_exit);
