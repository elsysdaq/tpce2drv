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

#ifndef TPCE_ALTERA_PCIE_H
#define TPCE_ALTERA_PCIE_H

/* Contains some configuration details of PCIe interface and a small utility for managing the PCIe page table */

/*******************************************************************************
 *  Portability
 ******************************************************************************/

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/stddef.h>
#include <linux/types.h>
#elif defined(_WIN32) && defined(_KERNEL_MODE)
#ifndef TPCE_STD_TYPE_COMPAT
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;
typedef BOOLEAN bool;
#endif
#else
#include <stdbool.h>
#include <stdint.h>
#endif

/* Support for static_assert */
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/build_bug.h>
#elif defined(_WIN32) && defined(_KERNEL_MODE)
/* Built-in support */
#elif (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
/* Built-in support */
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#include <assert.h>
#else
#error "No static_assert primitive found. Use C11/C++11 or later."
#endif

/* WARN_ONCE */
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/bug.h>
#elif defined(_WIN32) && defined(_KERNEL_MODE)
#ifndef WARN_ONCE
#define WARN_ONCE(condition, format, ...)                                                                 \
    do {                                                                                                  \
        static volatile LONG _warned = 0;                                                                 \
        if ((condition) && InterlockedCompareExchange(&_warned, 1, 0) == 0) {                             \
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "WARNING: %s:%d: " format "\n", __FILE__, \
                       __LINE__, ##__VA_ARGS__);                                                          \
        }                                                                                                 \
    } while (0)
#endif /* WARN_ONCE */
#else
#ifndef WARN_ONCE
#define WARN_ONCE(condition, format, ...)
#endif /* WARN_ONCE */
#endif

/* UINT8_MAX */
#ifndef UINT8_MAX
#define UINT8_MAX ((uint8_t)~0u)
#endif

/*******************************************************************************
 *  Constants
 ******************************************************************************/

#define ALTERA_PCIE_AVALON_ADDR_TRANSLATION_LOWER 0x1000
#define ALTERA_PCIE_AVALON_ADDR_TRANSLATION_UPPER 0x1004

#define ALTERA_ADDR_TRANSLATION_MASK_64 0xfffffffffffc0000
#define ALTERA_ADDR_TRANSLATION_MASK    0xfffc0000
#define ALTERA_ADDR_PAGE_SIZE           18 /* Bit-width */
#define ALTERA_ADDR_PAGE_SIZE_BYTES     (1 << 18)
#define ALTERA_ADDR_PAGE_TABLE_SIZE     16

/* See Cyclone V Hard IP for PCI Express User Guide (Table 8-30) */
#define ALTERA_ADDR_PAGE_TABLE_SP_32 0b00 /* Indicates page table entry should translate to 32 bit address */
#define ALTERA_ADDR_PAGE_TABLE_SP_64 0b01 /* Indicates page table entry should translate to 64 bit address */

/*******************************************************************************
 *  Address translation table management
 ******************************************************************************/

/* Local copy of page table for Altera PCIe IP to translate between Avalon-MM (32-bit) and x64 memory addresses */
/* The implementation is not thread-safe */

typedef struct {
    uint64_t pages[ALTERA_ADDR_PAGE_TABLE_SIZE];         /* Holds Masked 64-bit addresses */
    uint8_t active_channel[ALTERA_ADDR_PAGE_TABLE_SIZE]; /* DMA channel id */
    uint16_t used_mask;
} altera_pcie_page_table;
static_assert(ALTERA_ADDR_PAGE_TABLE_SIZE <= 8 * sizeof(uint16_t));

/* Returns 0-based index of first zero bit, or -1 if none found */
static inline int find_first_zero_u16(uint16_t mask) {
    /* Invert and find first set bit */
    uint16_t inverted = (uint16_t)~mask;
    if (inverted == 0) return -1; /* all bits set, no zero found */

#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, (unsigned long)inverted);
    return (int)index;

#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ffs(inverted) - 1;

#else
    /* Portable fallback */
    int n = 0;
    if ((inverted & 0x00FF) == 0) {
        n += 8;
        inverted >>= 8;
    }
    if ((inverted & 0x000F) == 0) {
        n += 4;
        inverted >>= 4;
    }
    if ((inverted & 0x0003) == 0) {
        n += 2;
        inverted >>= 2;
    }
    if ((inverted & 0x0001) == 0) {
        n += 1;
    }
    return n;
#endif
}

/* Allocate a slot */
static inline int pcie_page_alloc(altera_pcie_page_table *table, uint64_t value, uint8_t channel_id) {
    int slot = find_first_zero_u16(table->used_mask); /* find first zero bit */
    if (slot >= 16) return -1;

    table->pages[slot]          = value;
    table->active_channel[slot] = channel_id;
    table->used_mask |= (uint16_t)(1 << slot);
    return slot;
}

/* Free a slot */
static inline void pcie_page_free(altera_pcie_page_table *table, int slot) {
    if (slot < 0 || slot >= ALTERA_ADDR_PAGE_TABLE_SIZE) {
        WARN_ONCE(1, "Invalid pcie_page_table slot %d", slot);
        return;
    }
    WARN_ONCE(!(table->used_mask & (1u << slot)), "Double free of pcie_page_table slot %d", slot);

    table->pages[slot]          = 0;
    table->active_channel[slot] = 0;
    table->used_mask &= (uint16_t)~(1u << slot);
}

/* Free all slots used by a given channel (e.g. on channel teardown) */
static inline void pcie_page_free_by_channel_id(altera_pcie_page_table *table, uint8_t channel_id) {
    int slot;
    for (slot = 0; slot < ALTERA_ADDR_PAGE_TABLE_SIZE; slot++) {
        if (!(table->used_mask & (1u << slot))) continue;
        if (channel_id != UINT8_MAX && table->active_channel[slot] != channel_id) continue;
        pcie_page_free(table, slot);
    }
}

static inline int pcie_page_find_value(altera_pcie_page_table *table, uint64_t value, uint8_t channel_id) {
    int slot;
    for (slot = 0; slot < ALTERA_ADDR_PAGE_TABLE_SIZE; slot++) {
        if (!(table->used_mask & (1u << slot))) continue;
        if (channel_id != UINT8_MAX && table->active_channel[slot] != channel_id) continue;
        if (table->pages[slot] == value) return slot;
    }
    return -1;
}

static inline int pcie_page_active(altera_pcie_page_table *table, int slot, uint8_t channel_id) {
    return (table->used_mask & (1u << slot)) && (table->active_channel[slot] == channel_id);
}

/* Convert 64-bit bus address to its page-masked version */
static inline uint32_t pcie_translate_addr_64(uint64_t bus_addr, int slot) {
    if (slot < 0 || slot > ALTERA_ADDR_PAGE_TABLE_SIZE) {
        WARN_ONCE(1, "Invalid pcie_page_table slot %d", slot);
        return 0;
    }
    u32 offset = bus_addr & ~ALTERA_ADDR_TRANSLATION_MASK_64;
    return ((u32)slot << ALTERA_ADDR_PAGE_SIZE) | offset;
}

/* Convert the lower 32-bits of a bus address to its page-masked version */
static inline uint32_t pcie_translate_addr_32(uint32_t bus_addr_lo, int slot) {
    if (slot < 0 || slot > ALTERA_ADDR_PAGE_TABLE_SIZE) {
        WARN_ONCE(1, "Invalid pcie_page_table slot %d", slot);
        return 0;
    }
    u32 offset = bus_addr_lo & ~ALTERA_ADDR_TRANSLATION_MASK;
    return ((u32)slot << ALTERA_ADDR_PAGE_SIZE) | offset;
}

#endif /* TPCE_ALTERA_PCIE_H */
