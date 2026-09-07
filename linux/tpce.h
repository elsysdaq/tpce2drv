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
#ifndef TPCE_TPCE_H
#define TPCE_TPCE_H

#include "altera_msgdma.h"
#include "altera_pcie.h"
#include "tpce_public.h"

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>

#define _DRV_NAME_ "tpce2drv"

/* ""-DDEBUG"-only logging, due to being extremely noisy or exposing kernel memory layout */
#ifdef DEBUG
#define tpce_dev_dbg(dev, fmt, ...)             dev_dbg(dev, fmt, ##__VA_ARGS__)
#define tpce_dev_dbg_ratelimited(dev, fmt, ...) dev_dbg_ratelimited(dev, fmt, ##__VA_ARGS__)
#else
#define tpce_noop(...) \
    do {               \
    } while (0)
#define tpce_dev_dbg(dev, fmt, ...)             tpce_noop()
#define tpce_dev_dbg_ratelimited(dev, fmt, ...) tpce_noop()
#endif

#ifdef CONFIG_64BIT
#define TPCE_DMA_ADDRESS_WIDTH 64 /* Address width that device is able to access */
#else
#define TPCE_DMA_ADDRESS_WIDTH 32 /* Address width that device is able to access */
#endif

#define TPCE_DMA_DRIVER_BUFFER_ID   1             /* Id of kernel-space DMA buffer */
#define TPCE_DMA_DRIVER_BUFFER_SIZE (1024 * 1024) /* Buffer-size for legacy DMA API */
#define TPCE_DMA_MAP_ID_MAX         31            /* Specifies max ID for tpce->dma_map_ids */
#define TPCE_DMA_WAIT_TIMEOUT       2 * HZ        /* Default timeout for blocking DMA transaction */
#define TPCE_IOBUFFERSIZE           128           /* Size of locally allocated buffer for ioctl */
#define TPCE_MAP_INACTIVE           -1            /* Sentinel value for inactive DMA mappings */
#define TPCE_MAX_BOARDS             16            /* Maximum number of hardware devices */
#define TPCE_MAX_MSI                1             /* Maximum number of MSI interrupts */

/* Macros to generate virtual addresses for ioread/write */
#define BAR0_MEMORY_MAP(tpce, offset) ((void __iomem *)((u8 __iomem *)((tpce)->pci_bars[BAR_0].vaddr) + (offset)))
#define BAR2_MEMORY_MAP(tpce, offset) ((void __iomem *)((u8 __iomem *)((tpce)->pci_bars[BAR_2].vaddr) + (offset)))

typedef struct {
    resource_size_t paddr;
    resource_size_t size;
    void __iomem *vaddr; /* Nullable, Kernel-virtual address */
} pci_bar_info;

/* Generic kernel-memory buffer for ioctl syscalls */
typedef struct {
    struct tpce_file_ctx *file_ctx;
    void *io_buffer;
    u32 size;
} internal_ioctl_data;

typedef struct {
    struct list_head file_ctx_node; /* Sublist containing DMA mappings that belong to a specific file context */
    struct tpce_file_ctx *owner;    /* File context which mapped the buffer, NULL for driver-allocated buffers */
    void __user *buf_usr;           /* Non-NULL for userspace buffers locked for DMA */
    void *buf_drv;                  /* Non-NULL for driver-allocated kernel buffers */
    tpce_dma_map_id_t id;           /* ID for lookup in tpce->dma_map_idr */
    u32 size;                       /* Size of DMA-mapped region */
    u32 num_pages;                  /* Number of entries in pages[] array */
    u8 direction;                   /* According to <linux/dma_direction.h> */
    s8 active_channel;              /* Set to TPCE_MAP_INACTIVE to indicate that mapping is not in use */
    struct sg_table sgt;            /* Scatter-gather table containing DMA bus addresses */
    struct page *pages[];           /* List of virtual memory pages in the mapping */
} tpce_dma_map_info;

static inline bool mapping_is_busy(tpce_dma_map_info *info) {
    if (info->active_channel > TPCE_DMA_NUM_CHANNELS || info->active_channel < TPCE_MAP_INACTIVE) {
        WARN_ONCE(1, "DMA Mapping with ID %u has an illegal owner index", info->id);
        return false;
    }
    return info->active_channel >= 0;
}

typedef struct {
    spinlock_t events_lock; /* Protects status/pending between tasklet and process context */
    wait_queue_head_t wq;
    u32 status;
    bool pending;
} tpce_hw_events;

typedef struct tpce_dev tpce_dev;

typedef struct {
    /* Externally visible fields, for userspace "produced" is read-only, vice-versa for "consumed" */
    tpce_dma_stream_state *state;
    refcount_t state_refs; /* Refcount for "state" because it is mmap()ed to userspace */
    u64 irq_cnt;           /* Number of interrupts processed, used mainly for debugging purposes */
    s32 write_seq_num;     /* Sequence number of most recently processed descriptor */
    u32 num_desc_mask;     /* Is > 0 when num_descriptors is a power of two, to avoid divq in IRQ */
    u32 sync_offset_idx;   /* Current offset */
    u32 sync_offset;
} internal_dma_stream_info;

typedef struct {
    tpce_dev *tpce;                       /* Non-owning, reference to parent tpce device object */
    tpce_dma_transfer_params params;      /* Parameters of DMA transfer */
    struct mutex mutex;                   /* Dedicated mutex to synchronize state changes with IRQ workqueue */
    struct work_struct work;
    wait_queue_head_t wq;                 /* Wait Queue for DMA state changes */
    internal_dma_stream_info stream_info; /* Used during streaming DMA transfers */
    tpce_dma_map_info *map_info;          /* DMA mapping used for this DMA transaction */
    tpce_dma_descriptor *desc;            /* Unmapped copy of Memory Descriptors */
    tpce_dma_descriptor *desc_dma;        /* DMA-mapped Memory Descriptors (includes translation and some processing) */
    dma_addr_t desc_bus_addr;             /* Bus address of desc_dev buffer */
    u32 desc_bus_addr_translated;         /* Translated bus address */
    u32 desc_capacity;                    /* Allocated capacity for descriptors */
    u32 desc_size;                        /* Size (in bytes) of descriptors array */
    u32 desc_active; /* Size of the active portion of descriptors array (smaller or equal to desc_size) */
    s8 state;        /* See tpce_dma_channel_state */
} tpce_dma_info;

struct tpce_file_ctx {
    struct tpce_dev *tpce;            /* Device object which is associated with file handle */
    struct list_head dma_map_sublist; /* List to all DMA mapping IDs associated with file handle */
};

typedef struct tpce_dev {
    struct device *dev;     /* Device that we define ourselves (/dev/tpce0 etc.) */
    struct pci_dev *pcidev; /* PCI device information */
    struct cdev cdev;       /* Character device defines file operations */
    struct tasklet_struct tl_descr;
    struct mutex mutex;
    struct xarray dma_mappings;        /* Keeps track of user memory ranges pinned/mapped for DMA */
    altera_pcie_page_table page_table; /* Keeps track of the address translation table configuration */
    tpce_hw_events hw_events;
    int minor;

    pci_bar_info pci_bars[TPCE_NUM_BARS];
    tpce_dma_info dma_info[TPCE_DMA_NUM_CHANNELS];
    void *io_buffer; /* Buffer for IOCTL input/output */

    /* Hard IRQ data structures */
    spinlock_t irqsrc_lock; /* Protects tpce->irqsrc between ISR and tasklet */
    u32 irqsrc;             /* IRQ register */
    u32 fast_irq_mask;      /* Mask for interrupt vectors that are processed at hard IRQ level */

    refcount_t refs;
    bool going_away; /* Device is being removed */
} tpce_dev;

/* TODO Move these functions into a dedicated dma_info module such that we have: */
/* main.c, request.c, dma_hw.c, dma_info.c (or similar) */
/* Archecturally, I don't want locking to be done inside the dma_hw module */
/* The DMA IRQ bottom half should also move into a different place then */

/* Get DMA channel number from tpce_dma_info pointer */
/* Returns -1 if the pointer is invalid */
static inline int tpce_dma_ch(tpce_dma_info *info) {
    uintptr_t base   = (uintptr_t)info->tpce->dma_info;
    uintptr_t ptr    = (uintptr_t)info;
    size_t elem_size = sizeof(tpce_dma_info);

    if ((ptr - base) % elem_size != 0) {
        WARN_ONCE(1, "dma_info pointer is misaligned\n");
        return -1;
    }

    ptrdiff_t channel = (ptr - base) / elem_size;
    if (channel < 0 || channel >= TPCE_DMA_NUM_CHANNELS) {
        WARN_ONCE(1, "Illegal dma_info memory location (corresponds to channel %td)\n", channel);
        return -1;
    }
    return (int)channel;
}

/* Get DMA channel state in a concurrency-safe manner */
static inline tpce_dma_channel_state tpce_dma_state_locked(tpce_dev *tpce, tpce_dma_info *dma_info) {
    mutex_lock(&dma_info->mutex);
    tpce_dma_channel_state state = dma_info->state;
    mutex_unlock(&dma_info->mutex);
    return state;
}

/* Waits until a running DMA transfer transitions into another state */
static inline int tpce_dma_wait_until_completed(tpce_dev *tpce, tpce_dma_info *dma_info) {
    long ret = wait_event_killable_timeout(dma_info->wq, tpce_dma_state_locked(tpce, dma_info) != TPCE_DMA_RUNNING,
                                           TPCE_DMA_WAIT_TIMEOUT);

    if (ret < 0) {
        dev_dbg(tpce->dev, "DMA wait interrupted by signal on channel: %d\n", tpce_dma_ch(dma_info));
        return ret;
    }

    if (ret == 0) {
        dev_dbg(tpce->dev, "Timeout in DMA channel: %d\n", tpce_dma_ch(dma_info));
        return -ETIMEDOUT;
    }

    dev_dbg_ratelimited(tpce->dev, "DMA channel %d completed after %u us\n", tpce_dma_ch(dma_info),
                        (int)(TPCE_DMA_WAIT_TIMEOUT - ret));
    return 0;
}

#define RtlZeroMemory(pDest, ByteCount) memset((pDest), 0, (ByteCount))

#endif /* TPCE_TPCE_H */
