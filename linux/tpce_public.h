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

#ifndef TPCE_PUBLIC_H
#define TPCE_PUBLIC_H

/* Contains data structure definitions shared between driver platforms and userspace clients. Compatible with both C99,
 * C++11 and their subsequent revisions */

/* Only use standard C types in this file (i.e. no NT or linux-specific types) */

/*******************************************************************************
 *  Portability
 ******************************************************************************/

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200) /* Nonstandard extension used: zero-sized array in struct/union */
#endif

#if defined(__linux__) && defined(__KERNEL__)
/* Linux driver */
#include <linux/cache.h>
#include <linux/ioctl.h>
#include <linux/stddef.h>
#include <linux/types.h>
#elif defined(_WIN32) && defined(_KERNEL_MODE)
/* Windows Driver */
#include <ntddk.h>
#include <wdm.h>
#ifndef TPCE_WINNT_STD_TYPE_COMPAT
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;
typedef ULONG_PTR uintptr_t;
#endif
#elif defined(_WIN32)
/* Windows */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN /* Makes "windows.h" include fewer things */
#endif
#ifndef NOMINMAX
#define NOMINMAX /* Prevents "min" and "max" macros being defined */
#endif
#include <windows.h>
#include <winioctl.h>

#include "limits.h"
#include <stdint.h>
#else
/* Linux */
#include "limits.h"
#include <sys/ioctl.h>
#include <stddef.h>
#include <stdint.h>
#endif

/* __user is a Linux-kernel sparse annotation that marks pointers to user-space memory. */
#ifndef __user
#define __user
#endif

/* Portable alignment specifier */
#if (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
/* C++11/C23 and later use alignas keyword */
#define TPCE_ALIGNAS(n) alignas(n)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* C11 and later uses _Alignas */
#define TPCE_ALIGNAS(n) _Alignas(n)
#elif defined(_MSC_VER)
/* MSVC fallback */
#define TPCE_ALIGNAS(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__)
/* GCC & Clang fallback */
#define TPCE_ALIGNAS(n) __attribute__((__aligned__(n)))
#else
#error "TPCE_ALIGNAS: no alignment primitive found. Use C11/C++11 or later."
#endif

/* static_assert support */
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/build_bug.h>
#elif defined(_WIN32) && defined(_KERNEL_MODE)
/* Windows driver environment has built-in static_assert */
#elif (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
/* Also built-in for C++11/C23 and later */
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#if defined(_MSC_VER)
#include "assert.h"
#else
/* C11 has _Static_assert */
#define static_assert(expr, ...)        __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)
#endif /* _MSC_VER */
#elif defined(__GNUC__) || defined(__clang__)
/* Generic negative-size array trick as fallback */
#define static_assert(expr, msg) typedef char tpce_static_assert_##__LINE__[(expr) ? 1 : -1] __attribute__((unused))
#else
#define static_assert(expr, msg) typedef char tpce_static_assert_##__LINE__[(expr) ? 1 : -1]
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Constants
 ******************************************************************************/

/* Most CPUs as of 2026 have L1 cachelines with 64 bytes or less, so this a fairly safe bet */
#define TPCE_CACHELINE_SIZE 64

#define TPCE_AVALON_OFFSET           0x1000          /* Offset for FPGA application registers */
#define TPCE_AVALON_ADDR_DDR3_OFFSET 0x40000000      /* Offset for on-board DDR3 DMA addresses */
#define TPCE_QSYS_ID_ADDR            0x0400          /* Address for reading peripheral ID */
#define TPCE_QSYS_ENV_CSR            0x2000          /* PCIe config space offset */

#define TPCE_NUM_BARS                              2 /* Number of PCI BARs */
#define TPCE_DMA_NUM_CHANNELS                      3 /* Number of DMA controller channels */
#define TPCE_DMA_CSR_BASE_ADDR(channel)            (0x4000 + (channel) * 0x2000)
#define TPCE_DMA_PREFETCHER_CSR_BASE_ADDR(channel) (0x5000 + (channel) * 0x2000)
#define TPCE_DMA_CSR(channel, reg)                 (TPCE_DMA_CSR_BASE_ADDR(channel) + (reg))
#define TPCE_DMA_PREFETCHER_CSR(channel, reg)      (TPCE_DMA_PREFETCHER_CSR_BASE_ADDR(channel) + (reg))

/* Interrupt vectors (see QSYS) */
#define TPCE_IRQ_DMA0     0x001
#define TPCE_IRQ_PCIE     0x002
#define TPCE_IRQ_APP      0x004
#define TPCE_IRQ_I2C_BRD  0x008
#define TPCE_IRQ_I2C_AMP0 0x010
#define TPCE_IRQ_I2C_AMP1 0x020
#define TPCE_IRQ_I2C_EXT  0x040
#define TPCE_IRQ_SPI      0x080
#define TPCE_IRQ_DMA1     0x100
#define TPCE_IRQ_EPCS     0x200
#define TPCE_IRQ_DMA2     0x400

/* FPGA Application Interrupts */
#define TPCE_G2_INTSTAT (0x04 * 4)
#define TPCE_G2_INTCLR  (0x88 * 4)

/* Only these interrupt vectors are currently being used */
#define TPCE_IRQ_ENABLED (TPCE_IRQ_DMA0 | TPCE_IRQ_APP | TPCE_IRQ_DMA1 | TPCE_IRQ_DMA2)

#define ALTERA_PCIE_IRQ_STATUS_REG 0x40
#define ALTERA_PCIE_IRQ_ENABLE_REG 0x50

/*******************************************************************************
 *  Static checks
 ******************************************************************************/

/* Some platform compatibility checks */
#if defined(__linux__) && defined(__KERNEL__)
static_assert(__CHAR_BIT__ == 8);
static_assert(TPCE_CACHELINE_SIZE % L1_CACHE_BYTES == 0);
#elif defined(_WIN32) && defined(_KERNEL_MODE)
#else
static_assert(CHAR_BIT == 8);
#endif

/*******************************************************************************
 *  Shared data structures
 ******************************************************************************/

/* Please use uint8_t or bitfields instead of bool type */

/* General I/O data structure for POSIX ioctl syscalls */
typedef struct {
    unsigned char __user *in_buffer;
    size_t in_size;
    unsigned char __user *out_buffer;
    size_t out_size;
    /* int eventfd; >= 0: valid fd, -1: not used */
} ioctl_data;

typedef enum {
    BAR_0 = 0,
    BAR_2 = 1,
    BAR_4 = 2,
} T_BAR_SPACE;

typedef enum {
    REG_8  = 1,
    REG_16 = 2,
    REG_32 = 4,
    REG_64 = 8,
} T_PCI_DEV_REG_SIZE;

typedef enum {
    TPCE_MSGMDA_STANDARD_DESC          = 1,
    TPCE_MSGMDA_EXTENDED_DESC          = 2,
    TPCE_MSGMDA_PREFETCH_STANDARD_DESC = 3,
    TPCE_MSGMDA_PREFETCH_EXTENDED_DESC = 4,
} tpce_dma_descriptor_type_t;

/* Same as <linux/dma-direction.h> */
typedef enum {
    TPCE_DMA_BIDIRECTIONAL = 0,
    TPCE_DMA_TO_DEVICE     = 1,
    TPCE_DMA_FROM_DEVICE   = 2,
} tpce_dma_direction_t;

typedef struct {
    uint32_t offset;
    T_BAR_SPACE addrSpace;
    T_PCI_DEV_REG_SIZE size;
    uint32_t data;
    uintptr_t reserved;
} T_PCI_DEV_REG;

typedef struct {
    uint32_t offset;
    uint32_t data;
    T_PCI_DEV_REG_SIZE size;
    uint8_t is_write;
} T_PCI_CONF_REG;

/* For PCI BAR block read/write */
typedef struct {
    uint32_t offset;
    T_BAR_SPACE addrSpace;
    T_PCI_DEV_REG_SIZE size;
    uint32_t len;
    uint8_t data[];
} T_PCI_BLOCK;

typedef struct {
    void *pUserVA;
    uint32_t BytesCount;
    uint32_t localAddress;
    uint16_t readStride;
    uint16_t writeStride;
    uint8_t channel;
    uint8_t burst_count;
} T_DMA_REQUEST_DATA;

typedef struct {
    void __user *vaddr;
    uint32_t size;
} T_DMA_USER_BUFFER;

typedef struct {
    uint64_t bus_addr;
    uint32_t size;
    uint32_t _reserved;
} tpce_dma_range;

typedef uint32_t tpce_dma_map_id_t;
typedef struct {
    void __user *buf;              /* Pointer to userspace buffer */
    uint32_t size;                 /* Size (in bytes) of userspace buffer */
    tpce_dma_range __user *ranges; /* Pointer to array for storing tpce_dma_page information (ignored if NULL) */
    uint32_t ranges_size;          /* Size (in bytes) of pages buffer */
    uint8_t direction;             /* Mapping direction (tpce_dma_direction_t) */
} tpce_dma_buf_in;

typedef struct {
    tpce_dma_map_id_t id; /* Id of the DMA mapping */
    uint32_t num_pages;   /* Number of pages used for DMA buffer scatterlist */
} tpce_dma_buf_out;

typedef enum {
    TPCE_DMA_MODE_DEFAULT = 0, /* One-shot transfer */
    TPCE_DMA_MODE_STREAM  = 1, /* Endless cyclic/streaming transfer */
    /* TPCE_DMA_MODE_PINGPONG = 2, */
} tpce_dma_mode_t;

typedef struct {
    uint32_t map_offset;           /* Offset into DMA-mapped user buffer */
    uint32_t device_addr;          /* Address on hardware device */
    uint32_t bytes_total;          /* Size of transfer */
    uint32_t bytes_per_descriptor; /* 0 => flexible transfer size */
    uint16_t read_stride;
    uint16_t write_stride;
    uint8_t channel;
    uint8_t transfer_mode;      /* Transfer mode, must be a value contained in tpce_dma_mode_t */
    uint8_t direction;          /* Must be compatible with direction of DMA-mapping */
    uint8_t interrupt_interval; /* Interrupt flag is high every n-th descriptor, (0 = last descriptor triggers IRQ) */
    uint8_t burst_count;
    uint8_t _reserved[7];
} tpce_dma_transfer_params;
static_assert(sizeof(tpce_dma_transfer_params) == 32);

typedef struct {
    tpce_dma_map_id_t id; /* ID of DMA mapping */
    uint32_t _reserved1;
    tpce_dma_transfer_params params;
    unsigned char __user *descriptors; /* Nullable, Userspace pointer to descriptors array */
    uint32_t descriptors_size;         /* Size (in bytes) of the descriptors array */
    uint8_t descriptors_type;          /* Indicates type of data at descriptors array */
    uint8_t _reserved2[7];
} tpce_dma_request;

typedef enum {
    TPCE_DMA_IDLE        = 0, /* Channel is free */
    TPCE_DMA_INITIALIZED = 1, /* Resources allocated, no transfer is in progress */
    /* TPCE_DMA_READY       = 2, */
    TPCE_DMA_RUNNING  = 3, /* Transfer in progress */
    TPCE_DMA_COMPLETE = 4, /* Transfer is done */
    TPCE_DMA_ERROR    = 5, /* Error state, call free_dma_info/IOC_DMA_CANCEL to clear */
} tpce_dma_channel_state;

typedef struct {
    TPCE_ALIGNAS(TPCE_CACHELINE_SIZE) uint64_t produced;   /* Number of produced samples */
    uint8_t _pad0[TPCE_CACHELINE_SIZE - sizeof(uint64_t)]; /* Padding to prevent false sharing */
    TPCE_ALIGNAS(TPCE_CACHELINE_SIZE) uint64_t consumed;   /* Number of consumed samples */
    uint8_t _pad1[TPCE_CACHELINE_SIZE - sizeof(uint64_t)]; /* Padding to prevent false sharing */
} tpce_dma_stream_state;

typedef struct {
    uint32_t Address;              /* Die zu lesende Adresse                           */
    uint32_t Input;                /* Der zu lesende Kanal (1-4)                       */
    uint32_t DMA_Channel;
    uint32_t Mux8ChannelEn;        /* 4 or 8 channel module                            */
    uint32_t NextAddress;          /* Siehe addrNext                                   */
    uint32_t BlockSizeEnv;         /* Blockgroesse fuer die Huellkurvenberechnung      */
    uint32_t AnalogMaskEnv;        /* Maske fuer die Huellkurvenberechnung (nur 16bit!)*/
    uint32_t BlockStartEnv;        /* Startwert des ersten Blockes                     */
    uint32_t LastMinEnv;           /* Letzes Minimum                                   */
    uint32_t LastMaxEnv;           /* Letzes Maximum                                   */
    uint32_t AddrBlockSize;        /* Groesse des zu lesenden Blockes (2^n; n>=10)     */
    uint32_t AddrBlockNr;          /* Nummer des zu lesenden Blockes                   */
    uint32_t Count;                /* Anzahl der zu lesenden Bytes                     */
    unsigned char __user *Data;    /* Zeiger in den User-Space                         */
    uint32_t CountEnv;             /* Anzahl der zu lesenden Bytes (Huellkurve)        */
    unsigned char __user *DataEnv; /* Zeiger in den User-Space (Huellkurve)            */
    uint8_t burst_count;
} T_DMA_TPCE_DATA_READ;

/* TODO replace with tpce_dma_page type */
typedef struct {
    uint32_t PhysicalAddrLo;
    uint32_t PhysicalAddrHi;
    uint32_t length;
} T_SG_ENTRY;

typedef struct {
    uint32_t int_stat;
    uint32_t int_tpcx_stat;
} T_INT_STATUS;

/*******************************************************************************
 *  I/O control codes
 ******************************************************************************/

#ifdef _WIN32
#define TPCE_CTL_BASE    0x800
#define TPCE_CTL_CODE(n) CTL_CODE(FILE_DEVICE_UNKNOWN, TPCE_CTL_BASE + (n), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TPCE_IOC_PCI_BAR_READ        TPCE_CTL_CODE(0)
#define TPCE_IOC_PCI_BAR_WRITE       TPCE_CTL_CODE(1)
#define TPCE_IOC_PCI_BAR_BLOCK_READ  TPCE_CTL_CODE(2)
#define TPCE_IOC_PCI_BAR_BLOCK_WRITE TPCE_CTL_CODE(3)
#define TPCE_IOC_EVENT               TPCE_CTL_CODE(4)
#define TPCE_IOC_DMA_GETANDLOCK_SGL  TPCE_CTL_CODE(5)
#define TPCE_IOC_DMA_FREE_SGL        TPCE_CTL_CODE(6)
#define TPCE_IOC_DMA_FLUSH           TPCE_CTL_CODE(7)
#define TPCE_IOC_DMA_READ            TPCE_CTL_CODE(8)
#define TPCE_IOC_DMA_WRITE           TPCE_CTL_CODE(9)
#define TPCE_IOC_RW_CONFIGSPACE      TPCE_CTL_CODE(10)
#define TPCE_IOC_GET_INTSTAT         TPCE_CTL_CODE(11)
#define TPCE_IOC_DMA_READ2           TPCE_CTL_CODE(12)
#define TPCE_IOC_DMA_EXECUTE         TPCE_CTL_CODE(13) /* unimplemented */
#define TPCE_IOC_DMA_CANCEL          TPCE_CTL_CODE(14) /* unimplemented */
#define TPCE_IOC_DMA_STATUS          TPCE_CTL_CODE(15) /* unimplemented */
#else                                                  /* Linux */
#define IOTYPE                       0xc8
#define TPCE_IOC_PCI_BAR_READ        _IOWR(IOTYPE, 0, ioctl_data)
#define TPCE_IOC_PCI_BAR_WRITE       _IOWR(IOTYPE, 1, ioctl_data)
#define TPCE_IOC_PCI_BAR_BLOCK_READ  _IOWR(IOTYPE, 2, ioctl_data) /* unimplemented */
#define TPCE_IOC_PCI_BAR_BLOCK_WRITE _IOWR(IOTYPE, 3, ioctl_data) /* unimplemented */
#define TPCE_IOC_EVENT               _IOWR(IOTYPE, 4, ioctl_data)
#define TPCE_IOC_DMA_GETANDLOCK_SGL  _IOWR(IOTYPE, 5, ioctl_data)
#define TPCE_IOC_DMA_FREE_SGL        _IOWR(IOTYPE, 6, ioctl_data)
#define TPCE_IOC_DMA_FLUSH           _IOWR(IOTYPE, 7, ioctl_data)
#define TPCE_IOC_DMA_READ            _IOWR(IOTYPE, 8, ioctl_data)
#define TPCE_IOC_DMA_WRITE           _IOWR(IOTYPE, 9, ioctl_data)
#define TPCE_IOC_RW_CONFIGSPACE      _IOWR(IOTYPE, 10, ioctl_data)
#define TPCE_IOC_GET_INTSTAT         _IOWR(IOTYPE, 11, ioctl_data) /* unimplemented */
#define TPCE_IOC_DMA_READ2           _IOWR(IOTYPE, 12, ioctl_data)
#define TPCE_IOC_DMA_EXECUTE         _IOWR(IOTYPE, 13, ioctl_data)
#define TPCE_IOC_DMA_CANCEL          _IOWR(IOTYPE, 14, ioctl_data)
#define TPCE_IOC_DMA_STATUS          _IOWR(IOTYPE, 15, ioctl_data)
#endif

static inline const char *tpce_ioctl_name(unsigned int cmd) {
    switch (cmd) {
        case TPCE_IOC_PCI_BAR_READ:        return "PCI_BAR_READ";
        case TPCE_IOC_PCI_BAR_WRITE:       return "PCI_BAR_WRITE";
        case TPCE_IOC_PCI_BAR_BLOCK_READ:  return "PCI_BAR_BLOCK_READ";
        case TPCE_IOC_PCI_BAR_BLOCK_WRITE: return "PCI_BAR_BLOCK_WRITE";
        case TPCE_IOC_EVENT:               return "EVENT";
        case TPCE_IOC_DMA_GETANDLOCK_SGL:  return "DMA_GETANDLOCK_SGL";
        case TPCE_IOC_DMA_FREE_SGL:        return "DMA_FREE_SGL";
        case TPCE_IOC_DMA_FLUSH:           return "DMA_FLUSH";
        case TPCE_IOC_DMA_READ:            return "DMA_READ";
        case TPCE_IOC_DMA_WRITE:           return "DMA_WRITE";
        case TPCE_IOC_RW_CONFIGSPACE:      return "RW_CONFIGSPACE";
        case TPCE_IOC_GET_INTSTAT:         return "GET_INTSTAT";
        case TPCE_IOC_DMA_READ2:           return "DMA_READ2";
        case TPCE_IOC_DMA_EXECUTE:         return "DMA_EXECUTE";
        case TPCE_IOC_DMA_CANCEL:          return "DMA_CANCEL";
        case TPCE_IOC_DMA_STATUS:          return "DMA_STATUS";
        default:                           return "UNKNOWN";
    }
}

#ifdef __cplusplus
} /* extern "C"/ */
#endif

/*******************************************************************************
 *  mmap offsets
 ******************************************************************************/

#ifdef __linux__
/* Userspace passes these values multiplied by PAGE_SIZE as mmap()'s offset argument. */
/* The driver receives the unshifted value in vma->vm_pgoff. */
#define TPCE_MMAP_BAR0_OFF        (0x00 + 0)
#define TPCE_MMAP_BAR2_OFF        (0x00 + 2)
#define TPCE_MMAP_DMA0_STREAM_OFF (0x10 + 0)
#define TPCE_MMAP_DMA1_STREAM_OFF (0x10 + 1)
#define TPCE_MMAP_DMA2_STREAM_OFF (0x10 + 2)
#endif /* __linux__ */

/*******************************************************************************
 *  GUID Definition (Windows only)
 ******************************************************************************/

/* Use with #define INITGUID in a single translation unit of a program if intending to use the GUID definition */

#ifdef _WIN32
#include <guiddef.h>
DEFINE_GUID(GUID_DEVINTERFACE_TPCE, 0x34eaad29, 0xbd43, 0x4092, 0xba, 0x98, 0x13, 0xf2, 0xcf, 0x47, 0xdb, 0x9b);
#endif

/* Restore warning on MSVC */
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* TPCE_PUBLIC_H */
