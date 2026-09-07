#include "dma_descriptors.h"
#include "tpce_public.h"

// memset
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/string.h>
#else
#include <string.h>
#endif

// Run-time assertion
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/bug.h>
#define TPCE_ASSERT(cond) WARN_ON_ONCE(!(cond))
#elif defined(_WIN32) && defined(_KERNEL_MODE)
#include <wdm.h>
#define TPCE_ASSERT(cond) ASSERT(cond)
#else
#include <assert.h>
#define TPCE_ASSERT(cond) assert(cond)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TPCE_MAX(a, b)          ((a) > (b) ? (a) : (b))
#define TPCE_MIN(a, b)          ((a) > (b) ? (b) : (a))
#define TPCE_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

// clang-format off
const tpce_dma_channel_info tpce_dma_channel_info_table[3] = {
     // msgdma0
     {.data_width       = 8,
      .max_transfer     = 256 * 1024,
      .max_burst        = 32,
      .max_stride       = 0,
      .has_stream_read  = false,
      .has_stream_write = false},
     // msgdma1
     {.data_width       = 2,
      .max_transfer     = 256 * 1024,
      .max_burst        = 0,
      .max_stride       = 32768,
      .has_stream_read  = false,
      .has_stream_write = false},
     // msgdma2
     {.data_width       = 8,
      .max_transfer     = 256 * 1024,
      .max_burst        = 32,
      .max_stride       = 0,
      .has_stream_read  = true,
      .has_stream_write = false}};
// clang-format on

// Euclid's algorithm
static uint32_t gcd(uint32_t a, uint32_t b) {
    uint32_t t;
    while (b > 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// Struct that holds some intermediate data for descriptor generation
typedef struct {
    uint32_t gcd_page_sizes;   // GCD of all pages that are part of DMA mapping
    uint32_t num_descriptors;  // Number of descriptors needed
    uint32_t starting_idx;     // Index of first dma mapped page
    uint32_t starting_offset;  // Offset into first dma mapped page
} descriptor_plan;

// Get GCD of the DMA page sizes
static uint32_t gcd_dma_page_sizes(mapping_info *map_info) {
    uint32_t size = tpce_dma_map_num_ranges(map_info);
    if (size == 0) {
        return 0;
    }

    uint32_t res, curr, prev;
    res = prev = tpce_dma_map_range_size(map_info, 0);

    size_t i;
    for (i = 1; i < size; ++i) {
        curr = tpce_dma_map_range_size(map_info, i);
        if (prev == curr) {
            continue;
        }
        res  = gcd(res, curr);
        prev = curr;
    }
    return res;
}

static descriptor_plan create_descriptor_plan(mapping_info *map_info, tpce_dma_transfer_params *params) {
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];
    uint32_t dma_pages                   = tpce_dma_map_num_ranges(map_info);

    descriptor_plan out = {0};
    if (dma_pages == 0) {
        return out;
    }

    out.gcd_page_sizes = gcd_dma_page_sizes(map_info);
    uint32_t bytes_rem = params->bytes_total;
    uint32_t off_rem   = params->map_offset;
    uint32_t num_desc  = 0;
    uint32_t off_idx   = 0;

    size_t i;
    for (i = 0; i < dma_pages; ++i) {
        uint32_t size = tpce_dma_map_range_size(map_info, i);
        // Find offset into mapping
        if (off_rem >= size) {
            off_rem -= size;
            off_idx += 1;
        }
        else {
            break;
        }
    }

    if (params->bytes_per_descriptor == 0) {
        for (i = off_idx; i < dma_pages && bytes_rem > 0; ++i) {
            uint32_t size          = tpce_dma_map_range_size(map_info, i);
            uint32_t bytes_in_page = size - (i == off_idx ? off_rem : 0);
            uint32_t page_bytes    = TPCE_MIN(bytes_rem, bytes_in_page);

            num_desc += TPCE_DIV_ROUND_UP(page_bytes, ch_info->max_transfer);
            bytes_rem -= page_bytes;
        }
    }
    else {
        num_desc = params->bytes_total / params->bytes_per_descriptor;
    }

    out.starting_idx    = off_idx;
    out.starting_offset = off_rem;
    out.num_descriptors = num_desc;

    return out;
}

static bool valid_direction(uint8_t dma_direction, mapping_info *map_info) {
    uint8_t mapping_direction = tpce_dma_map_direction(map_info);

    bool valid_dma_dir      = (dma_direction == TPCE_DMA_FROM_DEVICE || dma_direction == TPCE_DMA_TO_DEVICE);
    bool compatible_mapping = (mapping_direction == TPCE_DMA_BIDIRECTIONAL || mapping_direction == dma_direction);

    return valid_dma_dir && compatible_mapping;
}

static bool valid_channel(uint8_t ch) {
    return ch < TPCE_DMA_NUM_CHANNELS;
}

uint32_t tpce_max_descriptors_needed(tpce_dma_transfer_params *params) {
    uint32_t ndesc;
    uint32_t bytes_per_desc = params->bytes_per_descriptor;
    if (bytes_per_desc > 0) {
        TPCE_ASSERT(params->bytes_total % bytes_per_desc == 0);
        ndesc = params->bytes_total / bytes_per_desc;
    }
    else {
        ndesc = TPCE_DIV_ROUND_UP(params->bytes_total, PAGE_SIZE);
    }
    return (params->transfer_mode == TPCE_DMA_MODE_DEFAULT ? ndesc + 1 : ndesc);
}

int tpce_validate_dma_params(tpce_dma_transfer_params *params, mapping_info *map_info) {
    int res = 0;
    if (!params || !map_info) {
        return -TPCE_ERR_NULL_PTR;
    }
    if (!valid_channel(params->channel)) {
        return -TPCE_ERR_INVALID_CHN;
    }
    const tpce_dma_channel_info ch_info = tpce_dma_channel_info_table[params->channel];

    // map_offset
    if (params->map_offset > tpce_dma_map_size(map_info) || params->map_offset % ch_info.data_width != 0) {
        res |= TPCE_PARAM_MAP_OFFSET;
    }

    // device_addr
    if (params->device_addr % ch_info.data_width != 0) {
        res |= TPCE_PARAM_DEVICE_ADDR;
    }

    // bytes_total
    // Could be made more lenient if we want to support wrap-around for the memory descriptors
    uint32_t total = params->bytes_total;
    if (total % ch_info.data_width != 0 ||  // Transfer size is not a multiple of the DMA data width
        total + params->map_offset > tpce_dma_map_size(map_info))  // Transfer overflows user buffer
    {
        res |= TPCE_PARAM_BYTES_TOTAL;
    }

    // bytes_per_descriptor
    uint32_t bytes_per_desc = params->bytes_per_descriptor;
    if (bytes_per_desc > 0) {
        if (bytes_per_desc % ch_info.data_width != 0) {
            // Minimum transfer size is violated
            res |= TPCE_PARAM_BYTES_PER_DESC;
        }
        else if (bytes_per_desc > ch_info.max_transfer) {
            res |= TPCE_PARAM_BYTES_PER_DESC;
        }
        uint32_t gcd = gcd_dma_page_sizes(map_info);
        if (gcd % bytes_per_desc != 0) {
            // Transfers must fit evenly into sglist
            res |= TPCE_PARAM_BYTES_PER_DESC;
        }
    }

    // read_stride and write_stride
    if (TPCE_MAX(params->read_stride, params->write_stride) > ch_info.max_stride) {
        res |= TPCE_PARAM_STRIDES;
    }

    // transfer_mode
    if (params->transfer_mode != TPCE_DMA_MODE_DEFAULT && params->transfer_mode != TPCE_DMA_MODE_STREAM) {
        res |= TPCE_PARAM_TRANSFER_MODE;
    }

    // direction
    if (!valid_direction(params->direction, map_info)) {
        res |= TPCE_PARAM_DIRECTION;
    }

    // interrupt_interval
    uint32_t num_descriptors =
        bytes_per_desc ? params->bytes_total / bytes_per_desc : tpce_dma_map_num_ranges(map_info);
    if (params->interrupt_interval > 0) {
        if (num_descriptors % params->interrupt_interval != 0) {
            res |= TPCE_PARAM_INT_INTERVAL;
        }
    }

    // burst_count
    if (params->burst_count > ch_info.max_burst) {
        res |= TPCE_PARAM_BURST_COUNT;
    }

    return res;
}

static uint32_t get_transfer_length(tpce_dma_transfer_params *params, uint32_t bytes_in_page_rem) {
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];
    if (params->bytes_per_descriptor > 0) {
        TPCE_ASSERT(bytes_in_page_rem % params->bytes_per_descriptor == 0);
        return params->bytes_per_descriptor;
    }
    else {
        uint32_t transfer_length = TPCE_MIN(bytes_in_page_rem, ch_info->max_transfer);

        TPCE_ASSERT(transfer_length % ch_info->data_width == 0);
        return transfer_length;
    }
}

// Preconditions: params is validated against map_info, according to tpce_validate_params
static void fill_addresses(tpce_dma_transfer_params *params, mapping_info *map_info, tpce_dma_descriptor *descriptors,
                           descriptor_plan plan) {
    TPCE_ASSERT(valid_direction(params->direction, map_info));
    tpce_dma_descriptor *curr_desc;

    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];
    uint32_t device_addr                 = params->device_addr;
    uint32_t map_idx                     = plan.starting_idx;
    uint32_t bytes_rem                   = params->bytes_total;

    // Outer loop iterates over pages in DMA mapping info and inner loop iterates inside of each page
    uint32_t desc_idx;
    for (desc_idx = 0; desc_idx < plan.num_descriptors && bytes_rem > 0; ++map_idx) {
        uint32_t page_offset       = map_idx == plan.starting_idx ? plan.starting_offset : 0;
        uint32_t size              = tpce_dma_map_range_size(map_info, map_idx);
        uint32_t bytes_in_page_rem = TPCE_MIN(size - page_offset, bytes_rem);
        uint64_t bus_addr          = tpce_dma_map_range_addr(map_info, map_idx) + page_offset;

        while (bytes_in_page_rem > 0) {
            TPCE_ASSERT(desc_idx < plan.num_descriptors);

            curr_desc = &descriptors[desc_idx];
            if (params->direction == TPCE_DMA_FROM_DEVICE) {
                tpce_desc_set_read_32(curr_desc, ch_info->has_stream_read ? 0 : device_addr);
                tpce_desc_set_write_64(curr_desc, bus_addr);
            }
            else {
                tpce_desc_set_read_64(curr_desc, bus_addr);
                tpce_desc_set_write_32(curr_desc, ch_info->has_stream_write ? 0 : device_addr);
            }
            uint32_t length = get_transfer_length(params, bytes_in_page_rem);

            curr_desc->transfer_length = length;
            bus_addr += length;
            device_addr += length;
            bytes_in_page_rem -= length;
            bytes_rem -= length;
            ++desc_idx;
        }
    }
}

static void fill_control_flags_default(tpce_dma_transfer_params *params, tpce_dma_descriptor *descriptors,
                                       descriptor_plan plan) {
    TPCE_ASSERT(plan.num_descriptors > 0);
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];

    uint32_t i;
    for (i = 0; i < plan.num_descriptors - 1; ++i) {
        // Early Done Enable flag is meaningless for Stream-to-MM configurations
        descriptors[i].control |=
            ch_info->has_stream_read ? 0 : ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_DONE_ENABLE_MASK;
    }

    // Generate IRQ when final descriptor is completed
    descriptors[plan.num_descriptors - 1].control |= ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSFER_COMPLETE_IRQ_MASK;
}

static void fill_control_flags_stream(tpce_dma_transfer_params *params, tpce_dma_descriptor *descriptors,
                                      descriptor_plan plan) {
    TPCE_ASSERT(plan.num_descriptors > 0);
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];

    uint32_t i;
    for (i = 0; i < plan.num_descriptors; ++i) {
        if ((params->interrupt_interval > 0) &&                                     //
            (i % params->interrupt_interval) == (params->interrupt_interval - 1u))  //
        {
            // Generate periodic IRQ
            descriptors[i].control |= ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSFER_COMPLETE_IRQ_MASK;
        }
        // Early Done Enable flag is meaningless for Stream-to-MM configurations
        descriptors[i].control |=
            ch_info->has_stream_read ? 0 : ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_DONE_ENABLE_MASK;
    }
}

static void fill_control_flags(tpce_dma_transfer_params *params, tpce_dma_descriptor *descriptors,
                               descriptor_plan plan) {
    switch (params->transfer_mode) {
        case TPCE_DMA_MODE_DEFAULT: fill_control_flags_default(params, descriptors, plan); return;
        case TPCE_DMA_MODE_STREAM:  fill_control_flags_stream(params, descriptors, plan); return;
        default:                    TPCE_ASSERT(0 && "Unreachable");
    }
}

static void fill_other_fields(tpce_dma_transfer_params *params, tpce_dma_descriptor *descriptors,
                              descriptor_plan plan) {
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[params->channel];

    uint32_t i;
    for (i = 0; i < plan.num_descriptors; ++i) {
        descriptors[i].sequence_number   = (uint16_t)i;
        descriptors[i].read_stride       = params->read_stride;
        descriptors[i].write_stride      = params->write_stride;
        descriptors[i].read_burst_count  = ch_info->has_stream_read ? 0 : params->burst_count;
        descriptors[i].write_burst_count = ch_info->has_stream_write ? 0 : params->burst_count;
    }
}

int tpce_generate_descriptors(tpce_dma_transfer_params *params, mapping_info *map_info,
                              tpce_dma_descriptor *descriptors, uint32_t max_descriptors) {
    if (!params || !map_info || !descriptors) {
        return -TPCE_ERR_NULL_PTR;
    }

    descriptor_plan plan = create_descriptor_plan(map_info, params);

    if (plan.num_descriptors > max_descriptors) {
        return -TPCE_ERR_DESC_BUF;
    }
    if (plan.num_descriptors == 0) {
        return 0;
    }

    memset(descriptors, 0, plan.num_descriptors * sizeof(tpce_dma_descriptor));

    fill_addresses(params, map_info, descriptors, plan);
    fill_control_flags(params, descriptors, plan);
    fill_other_fields(params, descriptors, plan);

    return plan.num_descriptors;
}

int tpce_fill_default_dma_params(mapping_info *map_info, uint8_t channel, tpce_dma_transfer_params *params) {
    if (!map_info || !params) {
        return -TPCE_ERR_NULL_PTR;
    }
    if (!valid_channel(channel)) {
        return -TPCE_ERR_INVALID_CHN;
    }
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[channel];

    memset(params, 0, sizeof(tpce_dma_transfer_params));

    params->map_offset           = 0;
    params->device_addr          = 0;
    params->bytes_total          = tpce_dma_map_size(map_info);
    params->bytes_per_descriptor = 0;
    params->read_stride          = ch_info->max_stride ? 1 : 0;
    params->write_stride         = ch_info->max_stride ? 1 : 0;
    params->channel              = channel;
    params->direction            = TPCE_DMA_FROM_DEVICE;
    params->interrupt_interval   = 0;
    params->burst_count          = ch_info->max_burst;

    TPCE_ASSERT(tpce_validate_dma_params(params, map_info) == 0);
    return 0;
}

int tpce_fill_streaming_dma_params(mapping_info *map_info, uint8_t channel, tpce_dma_transfer_params *params) {
    if (!map_info || !params) {
        return -TPCE_ERR_NULL_PTR;
    }
    if (!valid_channel(channel)) {
        return -TPCE_ERR_INVALID_CHN;
    }
    const tpce_dma_channel_info *ch_info = &tpce_dma_channel_info_table[channel];

    memset(params, 0, sizeof(tpce_dma_transfer_params));

    params->map_offset           = 0;
    params->device_addr          = 0;
    params->bytes_total          = tpce_dma_map_size(map_info);
    params->bytes_per_descriptor = 1024;
    params->read_stride          = ch_info->max_stride ? 1 : 0;
    params->write_stride         = ch_info->max_stride ? 1 : 0;
    params->channel              = channel;
    params->transfer_mode        = TPCE_DMA_MODE_STREAM;
    params->direction            = TPCE_DMA_FROM_DEVICE;
    params->interrupt_interval   = 4;
    params->burst_count          = ch_info->max_burst;

    TPCE_ASSERT(tpce_validate_dma_params(params, map_info) == 0);
    return 0;
}

#if (defined(__linux__) && defined(__KERNEL__))

static bool dma_range_in_sg(struct scatterlist *sg, u64 start, u64 end) {
    u64 sg_start = sg_dma_address(sg);
    u64 sg_end;

    return sg_dma_len(sg) && !check_add_overflow(sg_start, (u64)sg_dma_len(sg), &sg_end) && start >= sg_start &&
           end <= sg_end;
}

static bool dma_range_in_sgt(const struct sg_table *sgt, struct scatterlist **cached_sg, dma_addr_t addr, u32 len) {
    struct scatterlist *sg;
    u64 start = addr;
    u64 end;
    int i;

    if (!len || check_add_overflow(start, (u64)len, &end)) return false;

    if (*cached_sg && dma_range_in_sg(*cached_sg, start, end)) return true;

    for_each_sgtable_dma_sg(sgt, sg, i) {
        if (dma_range_in_sg(sg, start, end)) {
            *cached_sg = sg;
            return true;
        }
    }

    return false;
}

int tpce_validate_desc_mem(tpce_dma_map_info *map_info, tpce_dma_descriptor *descs, uint32_t ndesc,
                           bool check_read_addr) {
    struct scatterlist *cached_sg = NULL;

    if (!map_info || !descs) return -TPCE_ERR_NULL_PTR;

    u32 i;
    for (i = 0; i < ndesc; i++) {
        dma_addr_t addr = check_read_addr ? tpce_desc_get_read(&descs[i]) : tpce_desc_get_write(&descs[i]);

        if (!dma_range_in_sgt(&map_info->sgt, &cached_sg, addr, descs[i].transfer_length))
            return -TPCE_ERR_INVALID_DESC;
    }

    return 0;
}

#endif

#ifdef __cplusplus
}  // extern "C"/
#endif
