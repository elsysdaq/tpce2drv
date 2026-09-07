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
#ifndef TPCE_DMA_H
#define TPCE_DMA_H

#include "tpce.h"
#include "tpce_public.h"

/* The functions defined in this module are not thread-safe and require external synchronization */

int tpce_dma_init(tpce_dev *tpce);
void tpce_dma_free(tpce_dev *tpce);

tpce_dma_map_info *tpce_dma_alloc_buffer(tpce_dev *tpce, u32 size, enum dma_data_direction, tpce_dma_map_id_t fix_id);
void tpce_dma_free_buffer(tpce_dev *tpce, tpce_dma_map_info *map_info);
tpce_dma_map_info *tpce_dma_lock_user_buffer(tpce_dev *tpce, tpce_dma_buf_in *buffer_info);
void tpce_dma_unlock_user_buffer(tpce_dev *tpce, tpce_dma_map_info *map_info);
void tpce_dma_free_mapping(tpce_dev *tpce, tpce_dma_map_info *map_info);

int tpce_dma_alloc_descriptors_mem(tpce_dev *tpce, tpce_dma_info *info, uint32_t size);
void tpce_dma_free_descriptors_mem(tpce_dev *tpce, tpce_dma_info *info);

int tpce_dma_start(tpce_dev *tpce, tpce_dma_info *info);
int tpce_dma_stream_start(tpce_dev *tpce, tpce_dma_info *info);
void tpce_dma_stream_stop(tpce_dev *tpce, tpce_dma_info *info);
void tpce_dma_abort(tpce_dev *tpce, tpce_dma_info *info);

void tpce_dma_completion(tpce_dma_info *info);
void tpce_dma_sync_for_cpu(tpce_dma_info *info);
void tpce_dma_sync_for_device(tpce_dma_info *info);

/* True when the DMA transfer requires memory for an additional terminating descriptor */
static inline bool tpce_dma_has_terminating_descriptor(tpce_dma_transfer_params *params) {
    return params->transfer_mode != TPCE_DMA_MODE_STREAM;
}

/* Sync a single memory descriptor for cache coherence */
static __always_inline void tpce_dma_sync_for_cpu_partial(tpce_dma_info *info, u32 desc_idx) {
    tpce_dma_descriptor *desc = &info->desc[desc_idx];

#if TPCE_DMA_ADDRESS_WIDTH == 64
    dma_addr_t bus_addr = (dma_addr_t)desc->write_address_high << 32 | (dma_addr_t)desc->write_address_low;
#else
    dma_addr_t bus_addr = (dma_addr_t)desc->write_address_low;
#endif
    dma_sync_single_for_cpu(&info->tpce->pcidev->dev, bus_addr, desc->transfer_length, info->map_info->direction);
}

static __always_inline void tpce_dma_sync_for_device_partial(tpce_dma_info *info, u32 desc_idx) {
    tpce_dma_descriptor *desc = &info->desc[desc_idx];

#if TPCE_DMA_ADDRESS_WIDTH == 64
    dma_addr_t bus_addr = (dma_addr_t)desc->read_address_high << 32 | (dma_addr_t)desc->read_address_low;
#else
    dma_addr_t bus_addr = (dma_addr_t)desc->read_address_low;
#endif
    dma_sync_single_for_device(&info->tpce->pcidev->dev, bus_addr, desc->transfer_length, info->map_info->direction);
}

#endif  /* TPCE_DMA_H */
