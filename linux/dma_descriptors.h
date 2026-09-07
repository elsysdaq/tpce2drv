/*******************************************************************************
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

#ifndef TPCE_DMA_DESCRIPTORS_H
#define TPCE_DMA_DESCRIPTORS_H

#include "altera_msgdma.h"
#include "tpce_public.h"

/* C implementation of Altera mSGDMA memory descriptor functionality
 * Designed to be equally usable in both userspace and driver code
 *
 * The code is designed to be reasonably flexible in accommodating various parameters,
 * but currently only supports DMA configurations involving transfers between
 * host and device. */

/*******************************************************************************
 *  Portability
 ******************************************************************************/

/* DMA mapping info structure differs between different drivers and userspace code
 * Make sure to access the mapping_info type only through the provided macros */
#if defined(__linux__) && defined(__KERNEL__)
/* Expects map_info to be of type internal_dma_mapping_info */
#include "tpce.h"
typedef tpce_dma_map_info mapping_info;
#define tpce_dma_id(map_info)             ((map_info)->id)
#define tpce_dma_map_id(map_info)         ((map_info)->id)
#define tpce_dma_map_size(map_info)       ((map_info)->size)
#define tpce_dma_map_num_ranges(map_info) ((map_info)->sgt.nents)
#define tpce_dma_map_direction(map_info)  ((map_info)->direction)
#define tpce_dma_map_range_addr(map_info, i) \
    (sg_dma_address(&(map_info)->sgt.sgl[(i)]) + (map_info)->sgt.sgl[(i)].offset)
#define tpce_dma_map_range_size(map_info, i) (sg_dma_len(&(map_info)->sgt.sgl[(i)]))
#elif defined(_WIN32) && defined(_KERNEL_MODE)
typedef tpce_dma_mapping_info mapping_info;
#define tpce_dma_address(map_info, i) /* TODO */
#define tpce_dma_size(map_info, i)    /* TODO */
#else
/* Expects map_info to be of type tpce_dma_mapping_info (see dma_mapping.h) */
#include "dma_buffer.h"
typedef tpce_dma_mapping_info mapping_info;
#define tpce_dma_map_id(map_info)            ((map_info)->id)
#define tpce_dma_map_size(map_info)          ((map_info)->size)
#define tpce_dma_map_num_ranges(map_info)    ((map_info)->ranges_used)
#define tpce_dma_map_direction(map_info)     ((map_info)->direction)
#define tpce_dma_map_range_addr(map_info, i) ((map_info)->ranges[(i)].bus_addr)
#define tpce_dma_map_range_size(map_info, i) ((map_info)->ranges[(i)].size)
#include <common/sys_memory.h>
#define PAGE_SIZE (sysPageSize())
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 *  DMA Channel Properties
 ******************************************************************************/

/* Perhaps this stuff best goes into a seperate file, but this is easier */
#define TPCE_DMA_NUM_CHANNELS 3

typedef struct {
    /* Extended features and prefetcher options are assumed to be enabled */
    uint32_t data_width;   /* Datapath width in bytes */
    uint32_t max_transfer; /* Maximum transfer size per descriptor in bytes */

    uint32_t max_stride;   /* Maximum stride length, 0 if striding is not available, mutually exclusive with burst */
    uint8_t max_burst;     /* Maximum burst size, 0 if burst is not available */
    bool has_stream_read;  /* true if DMA controller is configured with streaming input interface
                            * (streaming to mem-mapped) */
    bool has_stream_write; /* true if DMA controller is configured with streaming output interface
                            * (mem-mapped to streaming) */
} tpce_dma_channel_info;

typedef struct {
    uint32_t page_offset;
    uint32_t num_descriptors;
} tpce_dma_descriptors_info;

extern const tpce_dma_channel_info tpce_dma_channel_info_table[TPCE_DMA_NUM_CHANNELS];

/*******************************************************************************
 *  Descriptor generation functionality
 ******************************************************************************/

/* Printf-compatible formatting for DMA descriptors */
#define TPCE_DMA_DESC_FMT     \
    "{"                       \
    "Read_addr=0x%08x:%08x "  \
    "write_addr=0x%08x:%08x " \
    "xfer_len=%u "            \
    "next_desc=0x%08x:%08x "  \
    "bytes_xferd=%u "         \
    "status=0x%04hx "         \
    "seq=%hu "                \
    "rd_burst=%hhu "          \
    "wr_burst=%hhu "          \
    "rd_stride=%hu "          \
    "wr_stride=%hu "          \
    "ctrl=0x%08x"             \
    "}"

/* clang-format off */
#define TPCE_DMA_DESC_FMTARG(desc)                                                                                \
    (desc)->read_address_high, (desc)->read_address_low, (desc)->write_address_high, (desc)->write_address_low,   \
    (desc)->transfer_length, (desc)->next_desc_ptr_high, (desc)->next_desc_ptr_low, (desc)->bytes_transfered,     \
    (desc)->status, (desc)->sequence_number, (desc)->read_burst_count, (desc)->write_burst_count,                 \
    (desc)->read_stride, (desc)->write_stride, (desc)->control
/* clang-format on */

/* Allow a maximum of 256 descriptors */
#define TPCE_MAX_DESCRIPTORS_SIZE (256 * sizeof(tpce_dma_descriptor))

typedef enum {
    /* Bitmask flags for which parameters are invalid */
    TPCE_PARAM_OK             = 0,
    TPCE_PARAM_MAP_OFFSET     = (1 << 0),
    TPCE_PARAM_DEVICE_ADDR    = (1 << 1),
    TPCE_PARAM_BYTES_TOTAL    = (1 << 2),
    TPCE_PARAM_BYTES_PER_DESC = (1 << 3),
    TPCE_PARAM_STRIDES        = (1 << 4),
    TPCE_PARAM_TRANSFER_MODE  = (1 << 5),
    TPCE_PARAM_DIRECTION      = (1 << 6),
    TPCE_PARAM_INT_INTERVAL   = (1 << 7),
    TPCE_PARAM_BURST_COUNT    = (1 << 8),
} tpce_dma_param_flags_t;

typedef enum {
    TPCE_ERR_NULL_PTR     = 1,
    TPCE_ERR_INVALID_CHN  = 2,
    TPCE_ERR_DESC_BUF     = 3, /* Descriptor buffer is too small */
    TPCE_ERR_INVALID_DESC = 4, /* Descriptor references memory outside the DMA mapping */
    TPCE_ERR_OTHER        = 5,
} tpce_dma_err_t;

typedef enum {
    TPCE_DMA_DESC_ADDR_READ,
    TPCE_DMA_DESC_ADDR_WRITE,
} tpce_dma_desc_addr_field;

static inline void tpce_desc_set_write_32(tpce_dma_descriptor *desc, uint32_t val) {
    desc->write_address_low  = val;
    desc->write_address_high = 0;
}

static inline void tpce_desc_set_write_64(tpce_dma_descriptor *desc, uint64_t val) {
    desc->write_address_low  = val & 0xffffffff;
    desc->write_address_high = val >> 32;
}

static inline void tpce_desc_set_read_32(tpce_dma_descriptor *desc, uint32_t val) {
    desc->read_address_low  = val;
    desc->read_address_high = 0;
}

static inline void tpce_desc_set_read_64(tpce_dma_descriptor *desc, uint64_t val) {
    desc->read_address_low  = val & 0xffffffff;
    desc->read_address_high = val >> 32;
}

static inline uint64_t tpce_desc_get_write(const tpce_dma_descriptor *desc) {
    return ((uint64_t)desc->write_address_high << 32) | desc->write_address_low;
}

static inline uint64_t tpce_desc_get_read(const tpce_dma_descriptor *desc) {
    return ((uint64_t)desc->read_address_high << 32) | desc->read_address_low;
}

/* Returns the maximum number of memory descriptors needed for a DMA transfer with the given parameters */
uint32_t tpce_max_descriptors_needed(tpce_dma_transfer_params *params);

/* Validate input parameters for DMA transfer. A non-zero result indicates an error where a positive value provides a
 * bitmask with the invalid parameters set to high according to the tpce_dma_param_flags_t enum. A negative result
 * indicates an error as specified in tpce_dma_err_t. */
int tpce_validate_dma_params(tpce_dma_transfer_params *params, mapping_info *map_info);

/* Returns a default set of DMA transfer parameters */
int tpce_fill_default_dma_params(mapping_info *map_info, uint8_t channel, tpce_dma_transfer_params *params);
int tpce_fill_streaming_dma_params(mapping_info *map_info, uint8_t channel, tpce_dma_transfer_params *params);

/* Returns number of valid descriptors on success, negative error code on error
 * Assumes that the transfer parameters are valid according to the rules in tpce_validate_dma_params */
int tpce_generate_descriptors(tpce_dma_transfer_params *params, mapping_info *map_info,
                              tpce_dma_descriptor *descriptors, uint32_t max_descriptors);

#if (defined(__linux__) && defined(__KERNEL__)) /* || (defined(_WIN32) && defined(_KERNEL_MODE)) */
/* Validate that DMA descriptors only reference memory inside of DMA-mapped region */
int tpce_validate_desc_mem(mapping_info *map_info, tpce_dma_descriptor *descriptors, uint32_t ndesc,
                           bool check_read_addr);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TPCE_DMA_DESCRIPTORS_H */
