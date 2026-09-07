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

#include "request.h"

#include "altera_msgdma.h"
#include "altera_pcie.h"
#include "dma.h"
#include "dma_descriptors.h"
#include "tpce.h"
#include "tpce_public.h"

#include <linux/dma-direction.h>
#include <linux/mm.h>
#include <linux/xarray.h>

/*******************************************************************************
 * Helper functions and public API functions
 ******************************************************************************/

// Initialize stream transfer info by allocating stream_state object and resetting all counters
// Needs to be synchronized with tpce->io_mutex
int tpce_init_stream_info(internal_dma_stream_info *info) {
    if (!(info->state)) {
        WARN_ONCE(refcount_read(&info->state_refs), "Non-zero refcount, but state is NULL");
        info->state = vmalloc_user(sizeof(tpce_dma_stream_state));
        if (!info->state) {
            return -ENOMEM;
        }
        refcount_set(&info->state_refs, 1);
    }
    else {
        refcount_inc(&info->state_refs);
    }

    info->state->produced = 0;
    info->state->consumed = 0;
    info->write_seq_num   = 0;
    info->irq_cnt         = 0;
    info->num_desc_mask   = 0;
#ifdef DEBUG
    info->state->_pad0[0] = 0xe1;
    info->state->_pad0[1] = 0x51;
    info->state->_pad0[2] = 0x50;
#endif

    tpce_dma_info *dma_info = container_of(info, tpce_dma_info, stream_info);
    u32 num_descriptors     = dma_info->desc_size / sizeof(tpce_dma_descriptor);
    if (is_power_of_2(num_descriptors)) {
        info->num_desc_mask = num_descriptors - 1;
    }
    return 0;
}

// Needs to be synchronized with tpce->io_mutex
void tpce_free_stream_info(internal_dma_stream_info *info) {
    if (refcount_dec_and_test(&info->state_refs)) {
        vfree(info->state);
        info->state = NULL;
    }
}

void tpce_safe_dma_unmap(tpce_dev *tpce, tpce_dma_map_info *map_info) {
    if (mapping_is_busy(map_info)) {
        dev_dbg(tpce->dev, "Freeing DMA mapping with id %u which is busy on channel %hhd\n", map_info->id,
                map_info->active_channel);
        tpce_dma_info *dma_info = &tpce->dma_info[map_info->active_channel];
        mutex_lock(&dma_info->mutex);
        tpce_dma_abort(tpce, dma_info);
        reset_dma_info(tpce, dma_info);
        mutex_unlock(&dma_info->mutex);
        wake_up(&dma_info->wq);
    }

    tpce_dma_unlock_user_buffer(tpce, map_info);
}

// Returns a negative error code if DMA request is invalid
static int validate_dma_request(tpce_dev *tpce, tpce_dma_request *req, struct tpce_file_ctx *file_ctx) {
    tpce_dma_map_info *map_info = xa_load(&tpce->dma_mappings, req->id);

    u8 channel = req->params.channel;
    if (channel >= TPCE_DMA_NUM_CHANNELS) {
        dev_dbg(tpce->dev, "Illegal DMA channel id: %u\n", channel);
        return -EINVAL;
    }
    tpce_dma_info *dma_info = &tpce->dma_info[channel];

    if (!map_info) {
        dev_dbg(tpce->dev, "Unable to locate DMA mapping with id: %u\n", req->id);
        return -EINVAL;
    }
    if (map_info->owner && map_info->owner != file_ctx) {
        dev_dbg(tpce->dev, "DMA mapping with ID %u is owned by different file descriptor context\n", req->id);
        return -EINVAL;
    }
    if (map_info->direction != TPCE_DMA_BIDIRECTIONAL && req->params.direction != map_info->direction) {
        dev_dbg(tpce->dev, "Requested DMA direction is incompatible with DMA mapping direction\n");
        return -EINVAL;
    }

    if (dma_info->state != TPCE_DMA_IDLE) {
        dev_dbg(tpce->dev, "DMA controller channel %u is busy (state %u)\n", channel, dma_info->state);
        return -EBUSY;
    }

    switch (req->params.direction) {
        case TPCE_DMA_BIDIRECTIONAL: return -EINVAL;  // Not supported currently
        case TPCE_DMA_TO_DEVICE:     break;
        case TPCE_DMA_FROM_DEVICE:   break;
        default:                     return -EINVAL;
    }

    // Validate user-provided descriptor parameters
    if (req->descriptors != NULL) {
        if (req->descriptors_size > TPCE_MAX_DESCRIPTORS_SIZE) {
            dev_dbg(tpce->dev, "Too many descriptors\n");
            return -EMSGSIZE;             // TODO Request too large call instead
        }
        switch (req->descriptors_type) {  // Check descriptor type and size
            case TPCE_MSGMDA_PREFETCH_EXTENDED_DESC:
                if (req->descriptors_size % sizeof(alt_msgdma_prefetcher_extended_descriptor) != 0) {
                    dev_dbg(tpce->dev, "Descriptor array size must be divisible by %zu\n",
                            sizeof(alt_msgdma_prefetcher_extended_descriptor));
                    return -EINVAL;
                }
                break;
            default: dev_dbg(tpce->dev, "Descriptor format type is not supported\n"); return -EINVAL;
        }
    }
    return 0;
}

// Initializes the channel's dma_info object and copies/generates memory descriptor data
// Success: Returns 0, Error: Negative error number
static int init_dma_info(tpce_dev *tpce, tpce_dma_info *dma_info, tpce_dma_request *req) {
    int err;
    s8 channel = (s8)req->params.channel;
    WARN_ONCE(dma_info != &tpce->dma_info[channel], "dma_info does not match dma request channel");
    tpce_dma_map_info *map_info = xa_load(&tpce->dma_mappings, req->id);
    if (!map_info) {
        dev_dbg(tpce->dev, "DMA Mapping does not exist");
        return -EIO;
    }

    bool using_user_desc = (req->descriptors != NULL);
    u32 desc_alloc_size  = using_user_desc ? req->descriptors_size
                                           : tpce_max_descriptors_needed(&req->params) * sizeof(tpce_dma_descriptor);
    if (tpce_dma_has_terminating_descriptor(&req->params)) {
        // Allocate one additional element for a terminating descriptor
        err = tpce_dma_alloc_descriptors_mem(tpce, dma_info, desc_alloc_size + sizeof(tpce_dma_descriptor));
    }
    else {
        err = tpce_dma_alloc_descriptors_mem(tpce, dma_info, desc_alloc_size);
    }

    // Allocate DMA descriptor memory
    if (err) {
        dev_dbg(tpce->dev, "Error allocating descriptor buffer: %pe\n", ERR_PTR(err));
        return err;
    }

    // Copy DMA descriptors if provided by userspace
    if (using_user_desc) {
        if (copy_from_user(dma_info->desc, req->descriptors, req->descriptors_size)) {
            dev_dbg(tpce->dev, "Error copying descriptors from user\n");
            return -EIO;
        }
        bool check_read_addr = req->params.direction == TPCE_DMA_TO_DEVICE;
        err = tpce_validate_desc_mem(map_info, dma_info->desc, req->descriptors_size / sizeof(tpce_dma_descriptor),
                                     check_read_addr);
        switch (err) {
            case 0:  //
                dev_dbg(tpce->dev, "Using userspace-generated DMA descriptors\n");
                break;
            case -TPCE_ERR_INVALID_DESC:
                dev_dbg(tpce->dev, "DMA descriptors specify memory range outside DMA-mapped region\n");
                fallthrough;
            default:
                // Other error
                return -EINVAL;
        }
        dma_info->desc_active = desc_alloc_size;
    }
    // Otherwise we generate descriptors ourselves
    else {
        err = tpce_validate_dma_params(&req->params, map_info);
        if (err > 0) {
            dev_dbg(tpce->dev, "Invalid DMA parameters (Error flag bitfield is 0x%x)\n", err);
            return -EINVAL;
        }
        else if (err == -TPCE_ERR_INVALID_CHN)
            return -EINVAL;
        else if (err < 0)
            return -EIO;

        u32 desc_alloc_num = desc_alloc_size / sizeof(tpce_dma_descriptor);
        int ret            = tpce_generate_descriptors(&req->params, map_info, dma_info->desc, desc_alloc_num);
        if (ret < 0) {
            dev_dbg(tpce->dev, "Error generating DMA descriptors (code %d)\n", ret);
            return -EINVAL;
        }
        u32 generated_desc_count = ret;

        dev_dbg(tpce->dev, "Generated DMA descriptors:\n");
        u32 i;
        for (i = 0; i < generated_desc_count; ++i) {
            dev_dbg(tpce->dev, "info->descriptors[%d]=" TPCE_DMA_DESC_FMT "\n", i,
                    TPCE_DMA_DESC_FMTARG(&dma_info->desc[i]));
        }
#ifdef DEBUG
        bool check_read_addr = req->params.direction == TPCE_DMA_TO_DEVICE;
        err                  = tpce_validate_desc_mem(map_info, dma_info->desc, generated_desc_count, check_read_addr);
        if (err == -TPCE_ERR_INVALID_DESC) {
            dev_err(tpce->dev, "Driver-generated DMA descriptors failed validation\n");
            return -EINVAL;
        }
#endif

        if (tpce_dma_has_terminating_descriptor(&req->params)) {
            dma_info->desc_size = (generated_desc_count + 1) * sizeof(tpce_dma_descriptor);
        }
        else {
            dma_info->desc_size = generated_desc_count * sizeof(tpce_dma_descriptor);
        }
        dma_info->desc_active = generated_desc_count * sizeof(tpce_dma_descriptor);
    }

    // Copy descriptors to coherent DMA buffer
    memcpy(dma_info->desc_dma, dma_info->desc, dma_info->desc_size);

    // Initialize streaming info fields
    if (req->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        err = tpce_init_stream_info(&dma_info->stream_info);
        if (err) {
            dev_dbg(tpce->dev, "Error in init_stream_info: %pe\n", ERR_PTR(err));
            return err;
        }
    }

    // Copy validated request data into associated dma_info[] slot
    memcpy(&dma_info->params, &req->params, sizeof(tpce_dma_transfer_params));
    dma_info->map_info       = map_info;
    dma_info->state          = TPCE_DMA_INITIALIZED;
    map_info->active_channel = channel;

    return channel;
}

// Resets transfer state after a DMA operation completes/fails.
// Does NOT free the descriptor buffer, it is retained for reuse.
// Not concurrency-safe!
void reset_dma_info(tpce_dev *tpce, tpce_dma_info *info) {
    int channel = tpce_dma_ch(info);
    if (channel < 0) {
        return;
    }

    switch (info->state) {
        case TPCE_DMA_IDLE:        return;
        case TPCE_DMA_INITIALIZED: break;
        case TPCE_DMA_RUNNING:
            dev_dbg(tpce->dev, "free_dma_info called with active transfer on channel %d, aborting transfer\n", channel);
            tpce_dma_abort(tpce, info);
            break;
        case TPCE_DMA_COMPLETE: break;
        case TPCE_DMA_ERROR:    break;
        default:
            WARN_ONCE(1, "free_dma_info called on channel %d with illegal state %hhu\n", channel, info->state);
            pcie_page_free_by_channel_id(&tpce->page_table, (u8)channel);
            info->state = TPCE_DMA_IDLE;
            return;
    }
    pcie_page_free_by_channel_id(&tpce->page_table, (u8)channel);

    // Release dma_mapping ownership and reset transfer state, but keep descriptor buffer
    if (info->map_info) {
        info->map_info->active_channel = TPCE_MAP_INACTIVE;
        info->map_info                 = NULL;
    }
    if (info->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        tpce_free_stream_info(&info->stream_info);
    }
    info->desc_size = 0;  // Mark buffer content as stale, capacity remains unchanged
    info->state     = TPCE_DMA_IDLE;
}

// Dispatch to DMA module
static int dma_dispatch(tpce_dev *tpce, tpce_dma_info *dma_info) {
    int err = 0;

    switch (dma_info->params.transfer_mode) {
        case TPCE_DMA_MODE_DEFAULT: err = tpce_dma_start(tpce, dma_info); break;
        case TPCE_DMA_MODE_STREAM:  //
            err = tpce_dma_stream_start(tpce, dma_info);
            break;
        default:
            dev_dbg(tpce->dev, "Unknown DMA Transfer mode: %u\n", dma_info->params.transfer_mode);
            err = -EINVAL;
            break;
    }

    return err;
}

static int dma_execute(tpce_dev *tpce, tpce_dma_request *req) {
    u8 channel = req->params.channel;
    if (channel >= TPCE_DMA_NUM_CHANNELS) {
        dev_dbg(tpce->dev, "Invalid DMA channel number: %u\n", req->params.channel);
        return -EINVAL;
    }

    tpce_dma_info *dma_info = &tpce->dma_info[channel];
    mutex_lock(&dma_info->mutex);
    // Initialize DMA transfer resources
    int err = init_dma_info(tpce, dma_info, req);
    if (err < 0) {
        mutex_unlock(&dma_info->mutex);
        dev_dbg(tpce->dev, "DMA Initialization error: %pe\n", ERR_PTR(err));
        return err;
    }

    // Start DMA transaction
    err = dma_dispatch(tpce, dma_info);
    mutex_unlock(&dma_info->mutex);
    if (err < 0) {
        dev_dbg(tpce->dev, "Error executing DMA operation: %pe\n", ERR_PTR(err));
        goto cleanup;
    }

    if (dma_info->params.transfer_mode == TPCE_DMA_MODE_STREAM) {
        return 0;
    }

    err = tpce_dma_wait_until_completed(tpce, dma_info);
    if (err < 0) {
        dev_dbg(tpce->dev, "Error waiting for DMA completion: %pe\n", ERR_PTR(err));
    }

cleanup:
    mutex_lock(&dma_info->mutex);
    reset_dma_info(tpce, dma_info);
    mutex_unlock(&dma_info->mutex);
    return err;
}

// Get pointer to kernel-allocated DMA buffer storage
static void *get_kernel_dma_ptr(tpce_dev *tpce) {
    tpce_dma_map_info *info = xa_load(&tpce->dma_mappings, TPCE_DMA_DRIVER_BUFFER_ID);
    if (!info) {
        WARN_ONCE(1, "Kernel DMA-Buffer not found");
        return NULL;
    }
    return info->buf_drv;
}

static int read_kernel_dma_buffer(tpce_dev *tpce, u32 device_addr, u32 bytes_total, u16 read_stride, u16 write_stride,
                                  u8 channel, u8 burst_count) {
    int err;
    const tpce_dma_channel_info *ch_info;

    if (channel >= TPCE_DMA_NUM_CHANNELS) {
        return -EINVAL;
    }
    if (bytes_total > TPCE_DMA_DRIVER_BUFFER_SIZE) {
        dev_dbg(tpce->dev, "Requested DMA transfer size of %u exceeds kernel DMA buffer size of %u\n", bytes_total,
                TPCE_DMA_DRIVER_BUFFER_SIZE);
        return -EINVAL;
    }

    ch_info              = &tpce_dma_channel_info_table[channel];
    tpce_dma_request req = {.id     = TPCE_DMA_DRIVER_BUFFER_ID,
                            .params = {.device_addr   = device_addr,
                                       .bytes_total   = bytes_total,
                                       .read_stride   = ch_info->max_stride ? read_stride : 0,
                                       .write_stride  = ch_info->max_stride ? write_stride : 0,
                                       .channel       = channel,
                                       .burst_count   = ch_info->max_burst ? burst_count : 0,
                                       .transfer_mode = TPCE_DMA_MODE_DEFAULT,
                                       .direction     = TPCE_DMA_FROM_DEVICE}};

    err = validate_dma_request(tpce, &req, NULL);
    if (err) {
        dev_dbg(tpce->dev, "Failed to validate DMA request: %pe\n", ERR_PTR(err));
        return err;
    }

    err = dma_execute(tpce, &req);
    if (err) {
        dev_dbg(tpce->dev, "Failed to execute DMA request: %pe\n", ERR_PTR(err));
        return err;
    }

    return 0;
}

static int copy_kernel_dma_to_user(tpce_dev *tpce, unsigned char __user *dst, u32 copy_offset, u32 copy_len) {
    void *kernel_dma = get_kernel_dma_ptr(tpce);

    if (!copy_len) return 0;

    if (!kernel_dma) return -ENODEV;

    if (copy_offset > TPCE_DMA_DRIVER_BUFFER_SIZE || copy_len > TPCE_DMA_DRIVER_BUFFER_SIZE - copy_offset) {
        dev_dbg(tpce->dev, "Requested copy range exceeds kernel DMA buffer size\n");
        return -EINVAL;
    }

    if (copy_to_user(dst, (u8 *)kernel_dma + copy_offset, copy_len)) return -EFAULT;

    return 0;
}

static int read_kernel_dma_to_user(tpce_dev *tpce, unsigned char __user *dst, u32 device_addr, u32 dma_len,
                                   u32 copy_offset, u32 copy_len, u16 read_stride, u16 write_stride, u8 channel,
                                   u8 burst_count) {
    int err;

    err = read_kernel_dma_buffer(tpce, device_addr, dma_len, read_stride, write_stride, channel, burst_count);
    if (err) return err;

    return copy_kernel_dma_to_user(tpce, dst, copy_offset, copy_len);
}

/*******************************************************************************
 * ioctl implementations
 ******************************************************************************/

// TODO modify into a function with inout params and error code return
static unsigned int ioc_bar_read(tpce_dev *tpce, T_PCI_DEV_REG *RegInfo) {
    resource_size_t bar_size;

    bar_size = tpce->pci_bars[RegInfo->addrSpace].size;
    if (RegInfo->offset > bar_size) {
        dev_err(tpce->dev, "Offset %d out of Bar Length %pa\n", RegInfo->offset, &bar_size);
        return 0;
    }

    void __iomem *io_addr = tpce->pci_bars[RegInfo->addrSpace].vaddr + RegInfo->offset;
    switch (RegInfo->size) {
        case REG_8:  return ioread8(io_addr);
        case REG_16: return ioread16(io_addr);
        case REG_32: return ioread32(io_addr);
        case REG_64:
            // Not supported
        default: return 0;
    }
}

static int ioc_bar_write(tpce_dev *tpce, T_PCI_DEV_REG *RegInfo) {
    resource_size_t bar_size;

    bar_size = tpce->pci_bars[RegInfo->addrSpace].size;
    if (RegInfo->offset > bar_size) {
        // "Register Offset is outside of the BAR, BarRegWrite
        dev_err(tpce->dev, "Offset %d out of Bar Length %pa\n", RegInfo->offset, &bar_size);
        return -EINVAL;
    }

    void __iomem *io_addr = tpce->pci_bars[RegInfo->addrSpace].vaddr + RegInfo->offset;
    switch (RegInfo->size) {
        case REG_8:  iowrite8((u8)RegInfo->data, io_addr); break;
        case REG_16: iowrite16((u16)RegInfo->data, io_addr); break;
        case REG_32: iowrite32(RegInfo->data, io_addr); break;
        case REG_64: break;
        default:     break;
    }
    return 0;
}

static int ioc_config_readwrite(tpce_dev *tpce, internal_ioctl_data *iodata) {
    T_PCI_CONF_REG *confreg_info = (T_PCI_CONF_REG *)(iodata->io_buffer);

    if (confreg_info->is_write) {
        // Write to config space
        // tpce->BusInterfaceStandard.SetBusData(tpce->BusInterfaceStandard.Context,
        // PCI_WHICHSPACE_CONFIG, &In->data, In->offset, length);
        switch (confreg_info->size) {
            case REG_8:  pci_write_config_byte(tpce->pcidev, confreg_info->offset, confreg_info->data); break;
            case REG_16: pci_write_config_word(tpce->pcidev, confreg_info->offset, confreg_info->data); break;
            case REG_32: pci_write_config_dword(tpce->pcidev, confreg_info->offset, confreg_info->data); break;
            case REG_64:
            default:     dev_dbg(tpce->dev, "Config space write: Register length not supported\n"); return -EINVAL;
        }
        dev_dbg(tpce->dev, "Config space write: offset 0x%04x = 0x%08x\n", confreg_info->offset, confreg_info->data);
    }
    else {
        u32 val = 0;
        switch (confreg_info->size) {
            case REG_8: {
                u8 reg;
                pci_read_config_byte(tpce->pcidev, confreg_info->offset, &reg);
                val = reg;
                break;
            }
            case REG_16: {
                u16 reg;
                pci_read_config_word(tpce->pcidev, confreg_info->offset, &reg);
                val = reg;
                break;
            }
            case REG_32: {
                pci_read_config_dword(tpce->pcidev, confreg_info->offset, &val);
                break;
            }
            case REG_64:
            default:     dev_dbg(tpce->dev, "Config space read: Register length not supported\n"); return -EINVAL;
        }
        dev_dbg(tpce->dev, "Config space read: offset 0x%04x = 0x%08x\n", confreg_info->offset, val);

        memcpy(iodata->io_buffer, &val, sizeof(val));
    }
    return 0;
}

// Adapters for legacy DMA ioctl API
static int ioc_dma_read_request(tpce_dev *tpce, internal_ioctl_data *iodata) {
    T_DMA_REQUEST_DATA *in = (T_DMA_REQUEST_DATA *)(iodata->io_buffer);

    return read_kernel_dma_to_user(tpce, in->pUserVA, in->localAddress, in->BytesCount, 0, in->BytesCount,
                                   in->readStride, in->writeStride, in->channel, in->burst_count);
}

static int ioc_dma_read_request_a(tpce_dev *tpce, internal_ioctl_data *iodata) {
    T_DMA_TPCE_DATA_READ *in = (T_DMA_TPCE_DATA_READ *)(iodata->io_buffer);
    u16 read_stride          = 1;
    u32 dev_addr             = TPCE_AVALON_ADDR_DDR3_OFFSET;
    u32 ch_addr;

    if (in == NULL) {
        return -EINVAL;
    }
    if (in->DMA_Channel >= TPCE_DMA_NUM_CHANNELS) {
        dev_dbg(tpce->dev, "Invalid DMA channel number: %u\n", in->DMA_Channel);
        return -EINVAL;
    }
    if (in->Count > TPCE_DMA_DRIVER_BUFFER_SIZE) {
        dev_dbg(tpce->dev, "Requested DMA transfer size of %u exceeds buffer maximum of %u\n", in->Count,
                TPCE_DMA_DRIVER_BUFFER_SIZE);
        return -EINVAL;
    }

    read_stride = (u16)in->NextAddress;

    unsigned int block_mask = ~((in->AddrBlockSize * 2) - 1);
    unsigned int a1;
    unsigned int a2;

    // Test for address roll-over insigned the block mask
    if (read_stride == 1) {
        a1 = ((in->Address * 2) & block_mask);
        a2 = ((in->Address * 2) + in->Count) & block_mask;
    }
    else {
        a1 = ((in->Address * 2) & block_mask);
        a2 = ((in->Address * 2) + in->Count * (read_stride)) & block_mask;
    }

    unsigned long block_addr = in->AddrBlockNr * in->AddrBlockSize * 2;
    unsigned int offset      = (in->Address % 4) * 2;
    unsigned int overread    = 8;
    if (offset == 0 && read_stride == 1) overread = 0;

    if (in->Mux8ChannelEn == 0) {
        switch (in->Input) {
            case 0:  ch_addr = 0; break;
            case 1:  ch_addr = 0x10000000; break;
            case 2:  ch_addr = 0x20000000; break;
            case 3:  ch_addr = 0x30000000; break;
            default: ch_addr = 0; break;
        }
    }
    else {
        switch (in->Input) {
            case 0:  ch_addr = 0; break;
            case 1:  ch_addr = (0x10000000 >> 1); break;
            case 2:  ch_addr = (0x20000000 >> 1); break;
            case 3:  ch_addr = (0x30000000 >> 1); break;
            case 4:  ch_addr = (0x40000000 >> 1); break;
            case 5:  ch_addr = (0x50000000 >> 1); break;
            case 6:  ch_addr = (0x60000000 >> 1); break;
            case 7:  ch_addr = (0x70000000 >> 1); break;
            default: ch_addr = 0; break;
        }
    }

    if (a1 == a2) {
        u32 dma_len;

        // no block size roll over
        if (((in->Count & 0xFFFFFFF8) + overread) < in->Count) overread += 8;

        dev_addr += ((in->Address * 2) & ~block_mask) + block_addr;
        dev_addr += ch_addr;
        dma_len = in->Count < 8 ? 16 : (in->Count & 0xFFFFFFF8) + overread;

        return read_kernel_dma_to_user(tpce, in->Data, dev_addr & 0xFFFFFFF8, dma_len, offset, in->Count, read_stride,
                                       1, (u8)in->DMA_Channel, in->burst_count);
    }
    else if (in->Count / 2 > in->AddrBlockSize) {
        int err;

        dev_addr += block_addr;  // To Add Channel Address
        dev_addr += ch_addr;

        err = read_kernel_dma_buffer(tpce, dev_addr & 0xFFFFFFF8, (in->Count & 0xFFFFFFF8) + overread, read_stride, 1,
                                     (u8)in->DMA_Channel, in->burst_count);
        if (err) return err;

        err = copy_kernel_dma_to_user(tpce, in->Data, in->Address * 2, (in->AddrBlockSize - in->Address) * 2);
        if (err) return err;

        return copy_kernel_dma_to_user(tpce, in->Data + (in->AddrBlockSize - in->Address) * 2, 0, in->Address * 2);
    }
    else {
        int err;

        // Read in two segments
        unsigned int masked_addr = (in->Address * 2) & ~block_mask;
        dev_addr += masked_addr + block_addr;
        dev_addr += ch_addr;

        unsigned int block_start_addr = masked_addr & (block_mask);
        unsigned int block_end_addr   = block_start_addr + (in->AddrBlockSize * 2);
        unsigned int lcount, lcount2;
        unsigned int offset = 0;
        if (read_stride == 1) {
            dev_addr &= 0xFFFFFFF8;
            lcount  = block_end_addr - masked_addr;
            lcount2 = in->Count - lcount;
        }
        else {
            // DMA Channel 2 has 2 Byte access
            dev_addr &= 0xFFFFFFFE;

            lcount = (block_end_addr - masked_addr) / (read_stride);

            unsigned int rest = (block_end_addr - masked_addr) % (read_stride);
            if (rest > 0) {
                lcount &= 0xFFFFFFFE;
                lcount += 2;
                offset = rest;
            }

            lcount2 = in->Count - lcount;
            if ((lcount + lcount2) != in->Count) {
                dev_err(tpce->dev, "Lcount Error\n");
                return -EINVAL;
            }
        }

        if (lcount > 0) {
            err = read_kernel_dma_to_user(tpce, in->Data, dev_addr, (lcount & 0xFFFFFFF8) + overread, offset, lcount,
                                          read_stride, 1, (u8)in->DMA_Channel, in->burst_count);
            if (err) return err;
        }
        if (lcount2 > 0) {
            dev_addr = TPCE_AVALON_ADDR_DDR3_OFFSET + block_start_addr + block_addr + offset;
            dev_addr += ch_addr;

            return read_kernel_dma_to_user(
                tpce, in->Data + lcount, dev_addr,
                ((lcount2 & 0xFFFFFFF8) + overread) < 8 ? 16 : (lcount2 & 0xFFFFFFF8) + overread, 0, lcount2,
                read_stride, 1, (u8)in->DMA_Channel, in->burst_count);
        }
    }
    return 0;
}

// TODO implement DMA write
static int ioc_dma_write_request(tpce_dev *tpce, internal_ioctl_data *iodata) {
    T_DMA_REQUEST_DATA *In;

    In = (T_DMA_REQUEST_DATA *)(iodata->io_buffer);
    return -ENOSYS;
}

static int ioc_lock_dma_buffer(tpce_dev *tpce, internal_ioctl_data *iodata) {
    tpce_dma_buf_out *info_out;
    tpce_dma_map_info *map_info;

    map_info = tpce_dma_lock_user_buffer(tpce, (tpce_dma_buf_in *)iodata->io_buffer);
    if (IS_ERR(map_info)) {
        return PTR_ERR(map_info);
    }
    map_info->owner = iodata->file_ctx;
    list_add_tail(&map_info->file_ctx_node, &iodata->file_ctx->dma_map_sublist);

    // Prepare IO-buffer
    memset(iodata->io_buffer, 0, sizeof(tpce_dma_buf_out));
    info_out = iodata->io_buffer;

    info_out->id        = map_info->id;
    info_out->num_pages = map_info->sgt.nents;

    return 0;
}

// Either refcount and block with -EBUSY or add a state machine with "PENDING_UNMAP" state and do the actual unmap in
// the DMA completion handle
static int ioc_unlock_dma_buffer(tpce_dev *tpce, internal_ioctl_data *iodata) {
    tpce_dma_map_id_t id = *(tpce_dma_map_id_t *)iodata->io_buffer;

    tpce_dma_map_info *map_info = xa_load(&tpce->dma_mappings, id);
    if (!map_info) {
        dev_dbg(tpce->dev, "Unable to locate DMA mapping with id %u\n", id);
        return -EINVAL;
    }
    if (map_info->owner != iodata->file_ctx) {
        dev_dbg(tpce->dev, "DMA mapping with ID %u is not owned by this fd\n", id);
        return -EPERM;
    }
    list_del(&map_info->file_ctx_node);
    tpce_safe_dma_unmap(tpce, map_info);
    return 0;
}

static int ioc_dma_execute(tpce_dev *tpce, internal_ioctl_data *iodata) {
    tpce_dma_request *req = (tpce_dma_request *)iodata->io_buffer;

    int err = validate_dma_request(tpce, req, iodata->file_ctx);
    if (err) {
        dev_dbg(tpce->dev, "Failed to validate DMA request: %pe\n", ERR_PTR(err));
        return err;
    }

    return dma_execute(tpce, req);
}

static int ioc_dma_cancel(tpce_dev *tpce, internal_ioctl_data *iodata) {
    u32 channel = *(u32 *)iodata->io_buffer;
    if (channel >= TPCE_DMA_NUM_CHANNELS) {
        return -EINVAL;
    }
    tpce_dma_info *dma_info = &tpce->dma_info[channel];

    mutex_lock(&dma_info->mutex);
    tpce_dma_abort(tpce, dma_info);
    reset_dma_info(tpce, dma_info);
    mutex_unlock(&dma_info->mutex);

    wake_up(&dma_info->wq);
    return 0;
}

// Adds a runtime check to confirm that user-provided buffer sizes are sufficient
// Only designed to work in the context of tpce_dispatch_ioc
#define validate_size(in_type, out_type)                                         \
    do {                                                                         \
        if (iodata->size < sizeof(in_type) || iodata->size < sizeof(out_type)) { \
            dev_dbg(tpce->dev, "Invalid buffer size\n");                         \
            return -EINVAL;                                                      \
        }                                                                        \
    } while (0)

// Bit of a hack: sizeof(int[0]) == 0 on GCC, so this typedef lets us write sizeof(none)
typedef int none[0];

int tpce_dispatch_ioc(tpce_dev *tpce, unsigned int ioc, internal_ioctl_data *iodata) {
    T_PCI_DEV_REG *RegInfo;

    // clang-format off
    switch (ioc) {
        case TPCE_IOC_PCI_BAR_READ:
            validate_size(T_PCI_DEV_REG, u32);
            RegInfo = (T_PCI_DEV_REG *)(iodata->io_buffer);
            *((u32 *)iodata->io_buffer) = ioc_bar_read(tpce, RegInfo);
            return 0;
        case TPCE_IOC_PCI_BAR_WRITE:
            validate_size(T_PCI_DEV_REG, none);
            RegInfo = (T_PCI_DEV_REG *)(iodata->io_buffer);
            return ioc_bar_write(tpce, RegInfo);
        case TPCE_IOC_PCI_BAR_BLOCK_READ:
        case TPCE_IOC_PCI_BAR_BLOCK_WRITE:
            goto not_implemented;
        case TPCE_IOC_EVENT:
            WARN_ONCE(1, "Unreachable\n");
            return -EIO;
        case TPCE_IOC_DMA_GETANDLOCK_SGL:
            validate_size(tpce_dma_buf_in, tpce_dma_buf_out);
            return ioc_lock_dma_buffer(tpce, iodata);
        case TPCE_IOC_DMA_FREE_SGL:
            validate_size(tpce_dma_map_id_t, none);
            return ioc_unlock_dma_buffer(tpce, iodata);
        case TPCE_IOC_DMA_FLUSH:
            goto not_implemented;
        case TPCE_IOC_DMA_READ:
            validate_size(T_DMA_REQUEST_DATA, none);
            return ioc_dma_read_request(tpce, iodata);
        case TPCE_IOC_DMA_WRITE:
            validate_size(T_DMA_REQUEST_DATA, none);
            return ioc_dma_write_request(tpce, iodata);
        case TPCE_IOC_RW_CONFIGSPACE:
            validate_size(T_PCI_CONF_REG, u32);
            return ioc_config_readwrite(tpce, iodata);
        case TPCE_IOC_DMA_READ2:
            validate_size(T_DMA_TPCE_DATA_READ, none);
            return ioc_dma_read_request_a(tpce, iodata);
        case TPCE_IOC_DMA_EXECUTE:
            validate_size(tpce_dma_request, none);
            return ioc_dma_execute(tpce, iodata);
        case TPCE_IOC_DMA_CANCEL:
            validate_size(u32, none);
            return ioc_dma_cancel(tpce, iodata);
        case TPCE_IOC_DMA_STATUS:
            WARN_ONCE(1, "Unreachable\n");
            return -EIO;
        not_implemented:
            dev_dbg(tpce->dev, "Not implemented ioctl code used: %s\n", tpce_ioctl_name(ioc));
            return -ENOSYS;
        default:
            dev_dbg(tpce->dev, "Unknown ioctl code: %u\n", ioc);
            return -ENOTTY;
    }
    WARN_ONCE(1, "Unreachable\n");
    return -EIO;
    // clang-format on
}

#undef validate_size
