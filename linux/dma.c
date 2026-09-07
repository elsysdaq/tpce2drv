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
#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include "dma.h"

#include "altera_msgdma.h"
#include "altera_pcie.h"
#include "dma_descriptors.h"
#include "tpce.h"
#include "tpce_public.h"

#include <linux/dma-mapping.h>
#include <linux/log2.h>
#include <linux/iopoll.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/version.h>

// Must set a special bit when programming the page table for PCIe core to generate 64-bit addresses
#if TPCE_DMA_ADDRESS_WIDTH == 64
#define PAGE_TABLE_SPACE_BIT ALTERA_ADDR_PAGE_TABLE_SP_64
#else
#define PAGE_TABLE_SPACE_BIT ALTERA_ADDR_PAGE_TABLE_SP_32
#endif

#define TPCE_DMA_RESET_POLL_DELAY_US         1
#define TPCE_DMA_RESET_POLL_TIMEOUT_US       1000
#define TPCE_DMA_STREAM_STOP_POLL_DELAY_US   5
#define TPCE_DMA_STREAM_STOP_POLL_TIMEOUT_US 1000

int tpce_dma_init(tpce_dev *tpce) {
    int ret;

    dev_dbg(&tpce->pcidev->dev, "Entering tpce_dma_init\n");
    // Initialize streaming DMA
    ret = dma_set_mask_and_coherent(&tpce->pcidev->dev, DMA_BIT_MASK(TPCE_DMA_ADDRESS_WIDTH));
    if (ret) {
        dev_err(&tpce->pcidev->dev, "Failed to set DMA mask\n");
        return ret;
    }

    // Initialize xarray for storing dynamic dma mapping information
    xa_init_flags(&tpce->dma_mappings, XA_FLAGS_ALLOC1);

    tpce_dma_map_info *info =
        tpce_dma_alloc_buffer(tpce, TPCE_DMA_DRIVER_BUFFER_SIZE, DMA_BIDIRECTIONAL, TPCE_DMA_DRIVER_BUFFER_ID);
    if (IS_ERR(info)) {
        dev_err(&tpce->pcidev->dev, "Failed to allocate DMA driver buffer: %pe\n", info);
        return PTR_ERR(info);
    }

    dev_dbg(&tpce->pcidev->dev, "Leaving tpce_dma_init\n");
    return 0;
}

void tpce_dma_free(tpce_dev *tpce) {
    unsigned long _id;
    tpce_dma_map_info *entry;
    xa_for_each(&tpce->dma_mappings, _id, entry) {
        if (entry->owner) {
            list_del(&entry->file_ctx_node);
        }
        tpce_dma_free_mapping(tpce, entry);
    }
    WARN_ON_ONCE(!xa_empty(&tpce->dma_mappings));
    xa_destroy(&tpce->dma_mappings);

    int i;
    tpce_dma_info *info;
    for (i = 0; i < TPCE_DMA_NUM_CHANNELS; ++i) {
        // Free DMA descriptor buffers
        info = &tpce->dma_info[i];
        tpce_dma_free_descriptors_mem(tpce, info);
    }
    dev_dbg(&tpce->pcidev->dev, "Leaving tpce_dma_free\n");
}

// Allocate a non-coherent DMA buffer mapped to kernel space. Size must be a power of two multiple of PAGE_SIZE.
// The caller may supply an argument fix_id > 0
// The dma_alloc_* API would be a simpler alternative to what we're doing here, but with this more manual approach we
// can use the same internal data structure and codepaths for kernel-mapped as well as user-mapped DMA transfers.
tpce_dma_map_info *tpce_dma_alloc_buffer(tpce_dev *tpce, u32 size, enum dma_data_direction dir,
                                         tpce_dma_map_id_t fixed_id) {
    tpce_dma_map_info *map_info;
    void *err = NULL;
    int ret;
    u32 id;

    if (dir == DMA_NONE) {
        return ERR_PTR(-EINVAL);
    }
    if (!size || !IS_ALIGNED(size, PAGE_SIZE)) {
        return ERR_PTR(-EINVAL);
    }
    u32 num_pages = size >> PAGE_SHIFT;
    if (!is_power_of_2(num_pages)) {
        return ERR_PTR(-EINVAL);
    }
    int order = ilog2(num_pages);

    // Allocate memory for internal mapping data in one block
    map_info = kvzalloc(struct_size(map_info, pages, num_pages), GFP_KERNEL);
    if (!map_info) {
        return ERR_PTR(-ENOMEM);
    }
    map_info->size           = size;
    map_info->num_pages      = num_pages;
    map_info->direction      = dir;
    map_info->active_channel = TPCE_MAP_INACTIVE;

    struct page *page = alloc_pages(GFP_KERNEL, order);
    if (!page) {
        dev_dbg(&tpce->pcidev->dev, "Failed to allocate driver-local buffer for DMA\n");
        err = ERR_PTR(-ENOMEM);
        goto error_alloc;
    }
    int i;
    for (i = 0; i < num_pages; i++) {
        map_info->pages[i] = page + i;
    }

    map_info->buf_drv = vmap(map_info->pages, num_pages, VM_MAP, PAGE_KERNEL);
    if (!map_info->buf_drv) {
        dev_dbg(&tpce->pcidev->dev, "Failed to vmap pages\n");
        err = ERR_PTR(-EIO);
        goto error_vmap;
    }

    ret = sg_alloc_table_from_pages(&map_info->sgt, map_info->pages, map_info->num_pages, 0, size, GFP_KERNEL);
    if (ret) {
        dev_dbg(&tpce->pcidev->dev, "Failed to build sg table: %pe\n", ERR_PTR(ret));
        err = ERR_PTR(ret);
        goto error_sgt;
    }

    ret = dma_map_sgtable(&tpce->pcidev->dev, &map_info->sgt, dir, 0);
    if (ret) {
        dev_dbg(&tpce->pcidev->dev, "Failed to DMA map sg table: %pe\n", ERR_PTR(ret));
        err = ERR_PTR(ret);
        goto error_map;
    }

    // Add DMA mapping information to dma_mappings xarray
    if (fixed_id) {
        ret = xa_insert(&tpce->dma_mappings, fixed_id, map_info, GFP_KERNEL);
        id  = fixed_id;
    }
    else {
        struct xa_limit limit = {.min = 1, .max = TPCE_DMA_MAP_ID_MAX};
        ret                   = xa_alloc(&tpce->dma_mappings, &id, map_info, limit, GFP_KERNEL);
    }
    if (ret < 0) {
        dev_dbg(&tpce->pcidev->dev, "Failed to allocate ID for DMA mapping: %pe\n", ERR_PTR(ret));
        err = ERR_PTR(ret);
        goto error_xa;
    }
    map_info->id = id;

    dev_dbg(&tpce->pcidev->dev,
            "Allocated driver-local DMA buffer: id %u info : vaddr 0x%p, bus_addr[0] 0x%pad, size %x, num_entries %u, "
            "num_pages %u\n",
            ret, map_info->buf_drv, &sg_dma_address(&map_info->sgt.sgl[0]), map_info->size, map_info->sgt.nents,
            map_info->num_pages);
    return map_info;
error_xa:
    dma_unmap_sgtable(&tpce->pcidev->dev, &map_info->sgt, map_info->direction, 0);
error_map:
    sg_free_table(&map_info->sgt);
error_sgt:
    vunmap(map_info->buf_drv);
error_vmap:
    __free_pages(map_info->pages[0], order);
error_alloc:
    kvfree(map_info);
    return err;
}

void tpce_dma_free_buffer(tpce_dev *tpce, tpce_dma_map_info *map_info) {
    WARN_ONCE(map_info->active_channel != TPCE_MAP_INACTIVE,
              "DMA buffer that was marked as in-use, has been unmapped\n");
    WARN_ON_ONCE(!is_power_of_2(map_info->num_pages));

    tpce_dma_map_id_t id = map_info->id;
    int order            = ilog2(map_info->num_pages);

    xa_erase(&tpce->dma_mappings, id);
    dma_unmap_sgtable(&tpce->pcidev->dev, &map_info->sgt, map_info->direction, 0);
    sg_free_table(&map_info->sgt);
    vunmap(map_info->buf_drv);
    __free_pages(map_info->pages[0], order);
    kvfree(map_info);

    dev_dbg(&tpce->pcidev->dev, "Freed DMA-mapped driver buffer with id %u\n", id);
}

// Copy the DMA bus addresses to userspace
// Returns 0 on success, negative error code on failure
static int copy_dma_page_info(tpce_dev *tpce, tpce_dma_buf_in *buffer_info, tpce_dma_map_info *map_info) {
    // Skip if user did not request copy of bus addresses
    if (!buffer_info->ranges) {
        return 0;
    }

    int num_entries = map_info->sgt.nents;
    int max_entries = (int)(buffer_info->ranges_size / sizeof(tpce_dma_range));
    if (num_entries > max_entries) {
        dev_dbg(tpce->dev, "Caller buffer too small, capacity: %d, needed: %d\n", max_entries, num_entries);
        return -ENOBUFS;
    }
    size_t size = num_entries * sizeof(tpce_dma_range);

    tpce_dma_range *temp_ranges = kvzalloc(size, GFP_KERNEL);
    if (!temp_ranges) {
        return -ENOMEM;
    }

    struct scatterlist *sg;
    int i;
    for_each_sgtable_dma_sg(&map_info->sgt, sg, i) {
        temp_ranges[i].bus_addr = sg_dma_address(sg);
        temp_ranges[i].size     = sg_dma_len(sg);

        dev_dbg(tpce->dev, "DMA Entry %d: Bus Addr %pad, Size %u\n", i, &temp_ranges[i].bus_addr, temp_ranges[i].size);
    }

    int ret = 0;
    if (copy_to_user(buffer_info->ranges, temp_ranges, size)) {
        ret = -EFAULT;
    }

    kvfree(temp_ranges);
    return ret;
}

// Returns an error pointer to the allocated dma mapping info structure
tpce_dma_map_info *tpce_dma_lock_user_buffer(tpce_dev *tpce, tpce_dma_buf_in *buffer_info) {
    tpce_dma_map_info *map_info;
    int ret;
    u32 id;

    void *err        = NULL;
    void __user *buf = buffer_info->buf;
    u32 page_offset  = (uintptr_t)buf & ~PAGE_MASK;
    int num_pages    = (int)(DIV_ROUND_UP(page_offset + buffer_info->size, PAGE_SIZE));

    dev_dbg(tpce->dev, "Pin DMA buffer with addr: %px, size: %x\n", buffer_info->buf, buffer_info->size);

    if (buffer_info->size % TPCE_CACHELINE_SIZE != 0) {
        dev_dbg(tpce->dev, "DMA buffer must be aligned to cacheline\n");
        return ERR_PTR(-EINVAL);
    }

    // Allocate memory for internal mapping data in one block
    map_info = kvzalloc(struct_size(map_info, pages, num_pages), GFP_KERNEL);
    if (!map_info) {
        return ERR_PTR(-ENOMEM);
    }
    map_info->buf_usr        = buf;
    map_info->size           = buffer_info->size;
    map_info->direction      = buffer_info->direction;
    map_info->active_channel = TPCE_MAP_INACTIVE;

    // Pin userspace pages so they don't get paged out
    unsigned gup_flags = FOLL_LONGTERM;
    if (buffer_info->direction == DMA_FROM_DEVICE || buffer_info->direction == DMA_BIDIRECTIONAL) {
        gup_flags |= FOLL_WRITE;
    }

    mmap_read_lock(current->mm);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
    long pages_pinned = pin_user_pages((uintptr_t)buf, num_pages, gup_flags, map_info->pages);
#else
    long pages_pinned = pin_user_pages((uintptr_t)buf, num_pages, gup_flags, map_info->pages, NULL);
#endif
    mmap_read_unlock(current->mm);

    if (pages_pinned < 0) {
        dev_dbg(tpce->dev, "Failed to pin user pages: %pe\n", ERR_PTR(pages_pinned));
        err = ERR_PTR(pages_pinned);
        goto error_pin;
    }

    map_info->num_pages = pages_pinned;
    if (pages_pinned < num_pages) {
        dev_dbg(tpce->dev, "Failed to pin all user pages, only %ld pages pinned\n", pages_pinned);
        err = ERR_PTR(-EFAULT);
        goto error_partial_pin;
    }

    // Build sg table
    ret = sg_alloc_table_from_pages(&map_info->sgt, map_info->pages, map_info->num_pages, page_offset,
                                    buffer_info->size, GFP_KERNEL);
    if (ret) {
        dev_dbg(tpce->dev, "Failed to build sg table: %pe\n", ERR_PTR(ret));
        goto error_sgt;
    }

    // Register sglist for DMA mapping
    ret = dma_map_sgtable(&tpce->pcidev->dev, &map_info->sgt, map_info->direction, 0);
    if (ret) {
        dev_dbg(tpce->dev, "Failed to create DMA map\n");
        err = ERR_PTR(ret);
        goto error_map;
    }
    map_info->direction = buffer_info->direction;

    ret = copy_dma_page_info(tpce, buffer_info, map_info);
    if (ret < 0) {
        dev_dbg(tpce->dev, "Failed to copy DMA addresses to user: %pe\n", ERR_PTR(ret));
        err = ERR_PTR(ret);
        goto error_copy_usr;
    }

    struct xa_limit limit = {.min = 1, .max = TPCE_DMA_MAP_ID_MAX};
    // Add DMA mapping information to dma_mappings xarray
    ret = xa_alloc(&tpce->dma_mappings, &id, map_info, limit, GFP_KERNEL);
    if (ret < 0) {
        dev_dbg(&tpce->pcidev->dev, "Failed to allocate ID for DMA mapping: %pe\n", ERR_PTR(ret));
        err = ERR_PTR(ret);
        goto error_xa;
    }
    map_info->id = id;

    dev_dbg(tpce->dev, "DMA map id %u info : vaddr %px, size %x, num_entries %u, num_pages %u\n", ret,
            map_info->buf_usr, map_info->size, map_info->sgt.nents, map_info->num_pages);
    return map_info;
error_xa:
error_copy_usr:
    dma_unmap_sgtable(&tpce->pcidev->dev, &map_info->sgt, map_info->direction, 0);
error_map:
    sg_free_table(&map_info->sgt);
error_sgt:
error_partial_pin:
    unpin_user_pages(map_info->pages, map_info->num_pages);
error_pin:
    kvfree(map_info);
    return err;
}

void tpce_dma_unlock_user_buffer(tpce_dev *tpce, tpce_dma_map_info *map_info) {
    tpce_dma_map_id_t id = map_info->id;  // Save id for debug print
    WARN_ONCE(map_info->active_channel != TPCE_MAP_INACTIVE,
              "DMA buffer that was marked as in-use, has been unmapped\n");

    xa_erase(&tpce->dma_mappings, id);
    dma_unmap_sgtable(&tpce->pcidev->dev, &map_info->sgt, map_info->direction, 0);
    sg_free_table(&map_info->sgt);
    unpin_user_pages(map_info->pages, map_info->num_pages);
    kvfree(map_info);

    dev_dbg(tpce->dev, "Freed DMA mapping with id %u\n", id);
}

void tpce_dma_free_mapping(tpce_dev *tpce, tpce_dma_map_info *map_info) {
    bool is_user_buf   = map_info->buf_usr != NULL;
    bool is_driver_buf = map_info->buf_drv != NULL;
    WARN_ON_ONCE(is_user_buf == is_driver_buf);
    if (is_user_buf) {
        tpce_dma_unlock_user_buffer(tpce, map_info);
    }
    else if (is_driver_buf) {
        tpce_dma_free_buffer(tpce, map_info);
    }
}

// Allocates a pair of buffers for memory descriptors. Reuses existing buffers if capacity is sufficient.
// Returns 0 on success, -EINVAL for illegal size argument and -ENOMEM if allocation failed
int tpce_dma_alloc_descriptors_mem(tpce_dev *tpce, tpce_dma_info *info, uint32_t size) {
    // Some defensive checks
    WARN_ONCE(!info->desc_dma && info->desc_capacity > 0, "Descriptor buffer is null, but has non-zero capacity\n");
    WARN_ONCE(info->desc_dma && info->desc_capacity == 0, "Descriptor buffer is non-null, but has zero capacity\n");
    WARN_ONCE(size % sizeof(tpce_dma_descriptor) != 0,
              "DMA Descriptor allocation size is not a multiple of descriptor size\n");
    if (size == 0 || size > INT_MAX) {
        return -EINVAL;
    }

    // Reuse existing buffer
    if (size <= info->desc_capacity) {
        info->desc_size = size;
        return 0;
    }

    // Free buffer that is currently allocated
    if (info->desc_dma) {
        dma_free_coherent(&tpce->pcidev->dev, info->desc_capacity, info->desc_dma, info->desc_bus_addr);
        kvfree(info->desc);
        info->desc                     = NULL;
        info->desc_dma                 = NULL;
        info->desc_bus_addr            = 0;
        info->desc_bus_addr_translated = 0;
        info->desc_size                = 0;
        info->desc_capacity            = 0;
    }

    // Allocate new buffer
    void *new_buffer, *new_buffer_dma;
    dma_addr_t new_bus_addr;
    new_buffer = kvzalloc(size, GFP_KERNEL);
    if (!new_buffer) {
        dev_dbg(tpce->dev, "Failed to allocate descriptor memory\n");
        return -ENOMEM;
    }
    new_buffer_dma = dma_alloc_coherent(&tpce->pcidev->dev, size, &new_bus_addr, GFP_KERNEL);
    if (!new_buffer_dma) {
        kvfree(new_buffer);
        dev_dbg(tpce->dev, "Failed to allocate DMA-coherent descriptor memory\n");
        return -ENOMEM;
    }
    info->desc                     = new_buffer;
    info->desc_dma                 = new_buffer_dma;
    info->desc_bus_addr            = new_bus_addr;
    info->desc_bus_addr_translated = 0;
    info->desc_size                = size;
    info->desc_capacity            = size;

    return 0;
}

// Frees the descriptor buffers for a channel. Generally this function only needs to be called when the entire device is
// deinitialized. Otherwise, setting info->desc_size = 0 should be sufficient for marking stale data.
void tpce_dma_free_descriptors_mem(tpce_dev *tpce, tpce_dma_info *info) {
    int channel = tpce_dma_ch(info);
    if (channel < 0) {
        return;
    }

    // This function being called on a non-idle channel is a serious bug
    switch (info->state) {
        case TPCE_DMA_IDLE:     break;
        case TPCE_DMA_RUNNING:  //
            tpce_dma_abort(tpce, info);
            __attribute__((__fallthrough__));
        default:
            WARN_ONCE(1, "tpce_dma_free_descriptors_mem called on channel %d with non-idle state %u\n", channel,
                      info->state);
            break;
    }

    if (info->desc_dma) {
        dma_free_coherent(&tpce->pcidev->dev, info->desc_capacity, info->desc_dma, info->desc_bus_addr);
    }
    kvfree(info->desc);
    info->desc                     = NULL;
    info->desc_dma                 = NULL;
    info->desc_bus_addr            = 0;
    info->desc_bus_addr_translated = 0;
    info->desc_size                = 0;
    info->desc_capacity            = 0;
}

static int translate_descriptor_link_addr(tpce_dev *tpce, tpce_dma_info *info) {
    int slot;
    u64 page_addr   = info->desc_bus_addr & ALTERA_ADDR_TRANSLATION_MASK_64;
    u64 page_offset = info->desc_bus_addr & ~ALTERA_ADDR_TRANSLATION_MASK_64;

    slot = pcie_page_alloc(&tpce->page_table, page_addr, info->params.channel);
    if (slot == -1) {
        dev_dbg(tpce->dev, "Address translation table is full\n");
        return -ENOSPC;
    }
    dev_dbg(tpce->dev, "Allocated slot %d for channel %u\n", slot, info->params.channel);
    // This works because an allocation with dma_alloc_coherent() guarantees that the bus address is aligned to
    // the next highest power of two. Therefore the descriptors array will not cross a pcie-page boundary.
    info->desc_bus_addr_translated = ((u32)slot << ALTERA_ADDR_PAGE_SIZE) | page_offset;
    WARN_ONCE(page_offset + info->desc_size >= ALTERA_ADDR_PAGE_SIZE_BYTES, "Unaligned descriptor buffer\n");
    dev_dbg(tpce->dev, "Descriptors Buffer Bus Addr: %pad -> PCIe Addr: %08x\n", &info->desc_bus_addr,
            info->desc_bus_addr_translated);

    return 0;
}

// Returns number of page table entries used, -ENOMEM on allocation failure, -ENOSPC when there is not enough space on
// the device for the address translation table
static int translate_descriptor_rw_addr(tpce_dev *tpce, tpce_dma_info *info) {
    int used_entries       = 0;
    int active_descriptors = info->desc_active / sizeof(*info->desc_dma);

    alt_msgdma_prefetcher_extended_descriptor *desc;
    u64 combined_addr, page_addr, curr_page = 0;
    int i, slot;
    switch (info->params.direction) {
        case DMA_FROM_DEVICE:
            for (i = 0; i < active_descriptors; ++i) {
                desc = &info->desc_dma[i];

                combined_addr = ((u64)desc->write_address_high << 32) | (u64)desc->write_address_low;
                page_addr     = combined_addr & ALTERA_ADDR_TRANSLATION_MASK_64;

                if ((curr_page != page_addr) &&
                    (slot = pcie_page_find_value(&tpce->page_table, page_addr, info->params.channel)) == -1) {
                    // Current bus addr range is not contained in page table. Allocate a new slot
                    slot = pcie_page_alloc(&tpce->page_table, page_addr, info->params.channel);
                    if (slot == -1) {
                        dev_dbg(tpce->dev, "Insufficient space for new address translation entries on device\n");
                        goto err_page_alloc;
                    }
                    dev_dbg(tpce->dev, "Allocated slot %d for channel %u\n", slot, info->params.channel);
                    curr_page = page_addr;
                    ++used_entries;
                }

                // Set descriptor addresses to translated values
                desc->write_address_low  = pcie_translate_addr_64(combined_addr, slot);
                desc->write_address_high = 0;
                dev_dbg(tpce->dev, "Data Buffer Bus Addr: 0x%016llx -> PCIe Addr: 0x%08x\n", combined_addr,
                        desc->write_address_low);
            }
            break;
        case DMA_TO_DEVICE:  //
            dev_dbg(tpce->dev, "DMA to device is not yet implemented\n");
            return -ENOSYS;
        default:  //
            return -EINVAL;
    }
    return used_entries;
err_page_alloc:
    pcie_page_free_by_channel_id(&tpce->page_table, info->params.channel);
    return -ENOSPC;
}

static void set_next_desc_ptr(alt_msgdma_prefetcher_extended_descriptor *desc, dma_addr_t val) {
    desc->next_desc_ptr_low = (u32)(val & 0xffffffff);
#if TPCE_DMA_ADDRESS_WIDTH == 64
    desc->next_desc_ptr_high = (u32)(val >> 32);
#else
    desc->next_desc_ptr_high = 0;
#endif
}

// Sets next_desc_ptr fields in the dma
static void build_descriptor_links(tpce_dma_info *info) {
    size_t size         = sizeof(*info->desc_dma);
    int num_descriptors = info->desc_size / sizeof(*info->desc_dma);
    u32 next_bus_addr   = info->desc_bus_addr_translated + size;

    alt_msgdma_prefetcher_extended_descriptor *desc;
    int i;
    for (i = 0; i < num_descriptors - 1; ++i) {
        desc = &info->desc_dma[i];

        set_next_desc_ptr(desc, next_bus_addr);
        desc->control |= ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_SET_MASK;

        next_bus_addr += size;
    }

    // Stream mode: make a circular descriptor list
    if (info->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        desc = &info->desc_dma[num_descriptors - 1];
        set_next_desc_ptr(desc, info->desc_bus_addr_translated);
        desc->control |= ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_SET_MASK;
    }
    // Normal mode: last descriptor is a dummy descriptor with "Owned by hw" flag set to 0
    else {
        desc = &info->desc_dma[num_descriptors - 1];
        // Zero the entire descriptor for good measure
        memset(desc, 0, sizeof(*info->desc_dma));
    }
}

static void print_descriptors(tpce_dev *tpce, tpce_dma_info *info) {
    size_t size         = sizeof(*info->desc_dma);
    int num_descriptors = info->desc_size / size;

    dev_dbg(tpce->dev, "Processed DMA descriptors:\n");
    int i;
    for (i = 0; i < num_descriptors; ++i) {
        dev_dbg(tpce->dev, "info->descriptors[%d]=" TPCE_DMA_DESC_FMT "\n", i,
                TPCE_DMA_DESC_FMTARG(&info->desc_dma[i]));
    }
}

static void set_page_translation_regs(tpce_dev *tpce, tpce_dma_info *info) {
    u32 tbl_hi, tbl_lo;
    altera_pcie_page_table *page_table = &tpce->page_table;
    int i;
    for (i = 0; i < ALTERA_ADDR_PAGE_TABLE_SIZE; ++i) {
        if (pcie_page_active(page_table, i, info->params.channel)) {
            tbl_lo = ((u32)page_table->pages[i]) & ALTERA_ADDR_TRANSLATION_MASK;
            tbl_lo |= PAGE_TABLE_SPACE_BIT;
            tbl_hi = (u32)(page_table->pages[i] >> 32);
            dev_dbg(tpce->dev, "Page %d: Set Translation Mask %08x:%08x\n", i, tbl_hi, tbl_lo);

            iowrite32(tbl_lo, BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_AVALON_ADDR_TRANSLATION_LOWER + 8 * i));
            iowrite32(tbl_hi, BAR2_MEMORY_MAP(tpce, ALTERA_PCIE_AVALON_ADDR_TRANSLATION_UPPER + 8 * i));
        }
    }
}

// static void prepare_waitqueue(tpce_dma_info *info) {}

static void start_dma_controller(tpce_dev *tpce, tpce_dma_info *info) {
    u8 channel         = info->params.channel;
    u32 dma_csr        = TPCE_DMA_CSR(channel, ALTERA_MSGDMA_CSR_CONTROL_REG);
    u32 prefetcher_csr = TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_CONTROL_REG);
    u32 prefetcher_next_desc_low_reg =
        TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_REG);
    u32 prefetcher_next_desc_high_reg =
        TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_REG);

    // Make sure dispatcher core control registers are unset
    u32 val = 0;
    iowrite32(val, BAR2_MEMORY_MAP(tpce, dma_csr));

    // Set prefetcher register to first element in descriptors array
    val = info->desc_bus_addr_translated;
    iowrite32(val, BAR2_MEMORY_MAP(tpce, prefetcher_next_desc_low_reg));
    iowrite32(0, BAR2_MEMORY_MAP(tpce, prefetcher_next_desc_high_reg));

    // Set global interrupt register in prefetcher
    val = ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_SET_MASK;
    // Set park mode bit for streaming transfer
    if (info->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        val |= ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_SET_MASK;
    }
    iowrite32(val, BAR2_MEMORY_MAP(tpce, prefetcher_csr));

    // Set prefetcher to the same value again with the "run" bit set as well
    val |= ALT_MSGDMA_PREFETCHER_CTRL_RUN_SET_MASK;
    iowrite32(val, BAR2_MEMORY_MAP(tpce, prefetcher_csr));
}

static int dma_start(tpce_dev *tpce, tpce_dma_info *info) {
    int ret;
    if (info->state != TPCE_DMA_INITIALIZED) {
        WARN_ONCE(1, "Invalid state of dma_info for dma stream start\n");
        return -EFAULT;
    }

    ret = translate_descriptor_link_addr(tpce, info);
    if (ret < 0) {
        dev_dbg(tpce->dev, "Error translating descriptor bus addr: %pe\n", ERR_PTR(ret));
        return ret;
    }
    ret = translate_descriptor_rw_addr(tpce, info);
    if (ret < 0) {
        dev_dbg(tpce->dev, "Error translating data bus addr: %pe\n", ERR_PTR(ret));
        info->state = TPCE_DMA_ERROR;  // Mark as error state since the DMA descriptors have been partially processed
        goto err_desc_translate;
    }
    build_descriptor_links(info);
    print_descriptors(tpce, info);  // Print descriptors to kernel log if debug messages are active
    set_page_translation_regs(tpce, info);

    start_dma_controller(tpce, info);  // Starts the DMA transfer

    return 0;
err_desc_translate:
    // Free PCIe page table entries
    pcie_page_free_by_channel_id(&tpce->page_table, info->params.channel);

    return ret;
}

// Precondition: tpce_dma_info struct has been initialized with init_dma_info
int tpce_dma_start(tpce_dev *tpce, tpce_dma_info *info) {
    int ret;
    WARN_ON_ONCE(info->desc_size != info->desc_active + sizeof(tpce_dma_descriptor));
    if ((ret = dma_start(tpce, info))) {
        goto err_dma_start;
    }

    info->state = TPCE_DMA_RUNNING;
    return 0;

err_dma_start:
    return ret;
}

// Enable hard IRQ handler for streaming DMA transfer
static int enable_stream_irq(tpce_dma_info *info) {
    u32 irqmask    = 0;
    tpce_dev *tpce = info->tpce;
    switch (tpce_dma_ch(info)) {
        case 0:  irqmask = TPCE_IRQ_DMA0; break;
        case 1:  irqmask = TPCE_IRQ_DMA1; break;
        case 2:  irqmask = TPCE_IRQ_DMA2; break;
        default: WARN_ONCE(1, "Received corrupted dma_info pointer"); return -EINVAL;
    }
    smp_store_release(&tpce->fast_irq_mask, tpce->fast_irq_mask | irqmask);
    dev_dbg(tpce->dev, "Set fast_irq_mask to: 0x%08x\n", tpce->fast_irq_mask);
    return 0;
}

// Disable hard IRQ handler for streaming DMA transfer
// Safe to call unconditionally
static void disable_stream_irq(tpce_dma_info *info) {
    u32 irqmask    = 0;
    tpce_dev *tpce = info->tpce;
    switch (tpce_dma_ch(info)) {
        case 0:  irqmask = TPCE_IRQ_DMA0; break;
        case 1:  irqmask = TPCE_IRQ_DMA1; break;
        case 2:  irqmask = TPCE_IRQ_DMA2; break;
        default: WARN_ONCE(1, "Received corrupted dma_info pointer"); return;
    }
    smp_store_release(&tpce->fast_irq_mask, tpce->fast_irq_mask & ~irqmask);
    dev_dbg(tpce->dev, "Set fast_irq_mask to: 0x%08x\n", tpce->fast_irq_mask);
}

// Precondition: tpce_dma_info struct has been initialized with init_dma_info
int tpce_dma_stream_start(tpce_dev *tpce, tpce_dma_info *info) {
    int ret;
    WARN_ON_ONCE(info->desc_size != info->desc_active);
    if ((ret = enable_stream_irq(info))) {
        return ret;
    }

    if ((ret = dma_start(tpce, info))) {
        goto err_dma_start;
    }

    info->state = TPCE_DMA_RUNNING;
    return 0;
err_dma_start:
    return ret;
}

// Resets the DMA controller to stop an active DMA transfer.
static void reset_dma_controller(tpce_dev *tpce, tpce_dma_info *info) {
    u8 channel                   = info->params.channel;
    u32 dma_csr_offset           = TPCE_DMA_CSR(channel, ALTERA_MSGDMA_CSR_CONTROL_REG);
    u32 prefetcher_csr_offset    = TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_CONTROL_REG);
    void __iomem *dma_csr        = BAR2_MEMORY_MAP(tpce, dma_csr_offset);
    void __iomem *prefetcher_csr = BAR2_MEMORY_MAP(tpce, prefetcher_csr_offset);
    u32 val;
    int ret;

    // Reset bit is cleared by hardware once reset has completed. Readx_poll_timeout_atomic spins on the control
    // register until the bit has been cleared. First we must reset the prefetcher, then the dispatcher core.

    u32 reset_prefetcher_mask = ALT_MSGDMA_PREFETCHER_CTRL_RESET_SET_MASK;
    iowrite32(reset_prefetcher_mask, prefetcher_csr);
    ret = readx_poll_timeout_atomic(ioread32, prefetcher_csr, val, !(val & reset_prefetcher_mask),
                                    TPCE_DMA_RESET_POLL_DELAY_US, TPCE_DMA_RESET_POLL_TIMEOUT_US);
    if (ret) {
        dev_err(tpce->dev, "Timeout waiting for prefetcher reset on channel %u\n", channel);
        return;
    }

    u32 reset_dispatcher_mask = ALTERA_MSGDMA_CSR_RESET_MASK;
    iowrite32(reset_dispatcher_mask, dma_csr);
    ret = readx_poll_timeout_atomic(ioread32, dma_csr, val, !(val & reset_dispatcher_mask),
                                    TPCE_DMA_RESET_POLL_DELAY_US, TPCE_DMA_RESET_POLL_TIMEOUT_US);
    if (ret) {
        dev_err(tpce->dev, "Timeout waiting for dispatcher reset on channel %u\n", channel);
    }
}

// Unconditionally resets dma controller
void tpce_dma_abort(tpce_dev *tpce, tpce_dma_info *info) {
    disable_stream_irq(info);
    reset_dma_controller(tpce, info);

    if (info->state != TPCE_DMA_RUNNING) {
        dev_dbg(tpce->dev, "tpce_dma_abort was called on already stopped channel %d\n", tpce_dma_ch(info));
        return;
    }
    // Abort moves to error state, since the software behavior to recover from an aborted transfer should be roughly the
    // same as from the error state
    info->state = TPCE_DMA_ERROR;
}

// Gracefully stops a streaming DMA operation by unsetting the park mode bit and waiting for the remaining DMA
// descriptors to be processed one last time.
void tpce_dma_stream_stop(tpce_dev *tpce, tpce_dma_info *info) {
    if (info->state != TPCE_DMA_RUNNING) {
        dev_dbg(tpce->dev, "tpce_dma_stream_stop called on already stopped channel %d\n", tpce_dma_ch(info));
        return tpce_dma_abort(tpce, info);
    }

    disable_stream_irq(info);

    int channel = tpce_dma_ch(info);
    void __iomem *prefetcher_csr =
        BAR2_MEMORY_MAP(tpce, TPCE_DMA_PREFETCHER_CSR(channel, ALT_MSGDMA_PREFETCHER_CONTROL_REG));

    u32 csr_state             = ioread32(prefetcher_csr);
    bool park_mode_is_running = csr_state & ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_SET_MASK &&
                                csr_state & ALT_MSGDMA_PREFETCHER_CTRL_RUN_SET_MASK;
    if (!park_mode_is_running) {
        dev_warn(tpce->dev, "Unexpected prefetcher CSR state: 0x%08x, aborting transfer instead of clean stop\n",
                 csr_state);
        return tpce_dma_abort(tpce, info);
    }

    u32 new_csr = csr_state & ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_CLR_MASK;
    iowrite32(new_csr, prefetcher_csr);

    u32 val;
    int ret = readx_poll_timeout_atomic(ioread32, prefetcher_csr, val, !(val & ALT_MSGDMA_PREFETCHER_CTRL_RUN_SET_MASK),
                                        TPCE_DMA_STREAM_STOP_POLL_DELAY_US, TPCE_DMA_STREAM_STOP_POLL_TIMEOUT_US);
    if (ret) {
        dev_warn(tpce->dev, "Timed out waiting for graceful DMA stream completion, aborting transfer\n");
        return tpce_dma_abort(tpce, info);
    }

    info->state = TPCE_DMA_COMPLETE;
    return;
}

// Handle DMA completion interrupts, must be synchronized externally
void tpce_dma_completion(tpce_dma_info *info) {
    // When streaming transfers are active the interrupt handling is done directly in the ISR, to help reduce latency.
    // Some interrupts might slip through in the stopping sequence, so they are explicitly ignored here.
    if (info->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        return;
    }

    // Update dma_info state
    switch (info->state) {
        case TPCE_DMA_RUNNING:
            info->state = TPCE_DMA_COMPLETE;
            // Handle old codepath which does not use map_info
            if (info->map_info && info->params.direction == TPCE_DMA_FROM_DEVICE) {
                tpce_dma_sync_for_cpu(info);
            }
            break;
        default:  // Ignore spurious completion interrupts
            break;
    }
}

// Sync entire DMA mapping for cache coherence
void tpce_dma_sync_for_cpu(tpce_dma_info *info) {
    dma_sync_sgtable_for_cpu(&info->tpce->pcidev->dev, &info->map_info->sgt, info->map_info->direction);
}

void tpce_dma_sync_for_device(tpce_dma_info *info) {
    dma_sync_sgtable_for_device(&info->tpce->pcidev->dev, &info->map_info->sgt, info->map_info->direction);
}
