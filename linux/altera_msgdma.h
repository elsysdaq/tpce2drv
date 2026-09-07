/******************************************************************************
 *                                                                             *
 * License Agreement                                                           *
 *                                                                             *
 * Copyright (c) 2014 Altera Corporation, San Jose, California, USA.           *
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
 * This agreement shall be governed in all respects by the laws of the State   *
 * of California and by the laws of the United States of America.              *
 *                                                                             *
 ******************************************************************************/

#ifndef ALTERA_MSGDMA_H
#define ALTERA_MSGDMA_H

/* Single header containing all definitions required for programming Altera
 * mSGDMA controller IP. For documentation see Embedded Peripherals IP User Guide
 * from Altera/Intel.
 
 * Original code by Altera Corporation with some additional definitions and
 * portability macros by Elsys AG, Switzerland */

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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type alignment specifiers */

/* C++11/C23 and later use alignas keyword */
#if (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#define ALT_MSGDMA_ALIGNAS(n) alignas(n)
/* C11 and later uses _Alignas */
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ALT_MSGDMA_ALIGNAS(n) _Alignas(n)
/* MSVC fallback */
#elif defined(_MSC_VER)
#define ALT_MSGDMA_ALIGNAS(n) __declspec(align(n))
/* GCC & Clang fallback */
#elif defined(__GNUC__) || defined(__clang__)
#define ALT_MSGDMA_ALIGNAS(n) __attribute__((__aligned__(n)))
#else
#error "ALT_MSGDMA_ALIGNAS: no alignment primitive found. Use C11/C++11 or later."
#endif

/* Struct packing directives */
#ifdef _MSC_VER
#define ALT_MSGDMA_PACK_BEGIN __pragma(pack(push, 1))
#define ALT_MSGDMA_PACK_END   __pragma(pack(pop))
#define ALT_MSGDMA_PACK_ATTR
#else  /* GCC & Clang */
#define ALT_MSGDMA_PACK_BEGIN
#define ALT_MSGDMA_PACK_END
#define ALT_MSGDMA_PACK_ATTR __attribute__((__packed__))
#endif  /* _MSC_VER */

/*******************************************************************************
 *  mSGDMA Data Structures
 ******************************************************************************/

/*
 * Helper struct to have easy access to hi/low values from a 64 bit value.
 * Useful when having to write prefetcher/descriptor 64 bit addresses.
 */
typedef union {
    uint64_t u64;
    uint32_t u32[2];
} msgdma_addr64;

/* Callback routine type definition */
typedef void (*alt_msgdma_callback)(void *context);

ALT_MSGDMA_PACK_BEGIN

/* use this structure if you haven't enabled the enhanced features */
typedef struct {
    ALT_MSGDMA_ALIGNAS(16) uint32_t *read_address;
    uint32_t *write_address;
    uint32_t transfer_length;
    uint32_t control;
} ALT_MSGDMA_PACK_ATTR alt_msgdma_standard_descriptor;

/* use this structure if you have enabled the enhanced features (only the
 * elements enabled in hardware will be used)
 */
typedef struct {
    ALT_MSGDMA_ALIGNAS(32) uint32_t *read_address_low;
    uint32_t *write_address_low;
    uint32_t transfer_length;
    uint16_t sequence_number;
    uint8_t read_burst_count;
    uint8_t write_burst_count;
    uint16_t read_stride;
    uint16_t write_stride;
    uint32_t *read_address_high;
    uint32_t *write_address_high;
    uint32_t control;
} ALT_MSGDMA_PACK_ATTR alt_msgdma_extended_descriptor;

/* Prefetcher Descriptors need to be different than standard dispatcher
 * descriptors use this structure if you haven't enabled the enhanced
 * features
 */
typedef struct {
    ALT_MSGDMA_ALIGNAS(32) uint32_t read_address;
    uint32_t write_address;
    uint32_t transfer_length;
    uint32_t next_desc_ptr;
    uint32_t bytes_transfered;
    uint16_t status;
    uint16_t _pad1_rsvd;
    uint32_t _pad2_rsvd;
    uint32_t control;
} ALT_MSGDMA_PACK_ATTR alt_msgdma_prefetcher_standard_descriptor;

/* use this structure if you have enabled the enhanced features (only the
 * elements enabled in hardware will be used)
 */
typedef struct {
    ALT_MSGDMA_ALIGNAS(64) uint32_t read_address_low;
    uint32_t write_address_low;
    uint32_t transfer_length;
    uint32_t next_desc_ptr_low;
    uint32_t bytes_transfered;
    uint16_t status;
    uint16_t _pad1_rsvd;
    uint32_t _pad2_rsvd;
    uint16_t sequence_number;
    uint8_t read_burst_count;
    uint8_t write_burst_count;
    uint16_t read_stride;
    uint16_t write_stride;
    uint32_t read_address_high;
    uint32_t write_address_high;
    uint32_t next_desc_ptr_high;
    uint32_t _pad3_rsvd[3];
    uint32_t control;
} ALT_MSGDMA_PACK_ATTR alt_msgdma_prefetcher_extended_descriptor, tpce_dma_descriptor, *PDMA_TRANSFER_ELEMENT;

ALT_MSGDMA_PACK_END

/*******************************************************************************
 *  mSGDMA CSR Register Definitions
 ******************************************************************************/

/*
  Enhanced features off:

  Bytes     Access Type     Description
  -----     -----------     -----------
  0-3       R/Clr           Status(1)
  4-7       R/W             Control(2)
  8-12      R               Descriptor Fill Level(write fill level[15:0], read
                            fill level[15:0])
  13-15     R               Response Fill Level[15:0]
  16-31     N/A             <Reserved>


  Enhanced features on:

  Bytes     Access Type     Description
  -----     -----------     -----------
  0-3       R/Clr           Status(1)
  4-7       R/W             Control(2)
  8-12      R               Descriptor Fill Level (write fill level[15:0], read
                            fill level[15:0])
  13-15     R               Response Fill Level[15:0]
  16-20     R               Sequence Number (write sequence number[15:0], read
                            sequence number[15:0])
  21-31     N/A             <Reserved>

  (1)  Writing a '1' to the interrupt bit of the status register clears the
       interrupt bit (when applicable), all other bits are unaffected by writes.
  (2)  Writing to the software reset bit will clear the entire register
       (as well as all the registers for the entire msgdma).

  Status Register:

  Bits      Description
  ----      -----------
  0         Busy
  1         Descriptor Buffer Empty
  2         Descriptor Buffer Full
  3         Response Buffer Empty
  4         Response Buffer Full
  5         Stop State
  6         Reset State
  7         Stopped on Error
  8         Stopped on Early Termination
  9         IRQ
  10-31     <Reserved>

  Control Register:

  Bits      Description
  ----      -----------
  0         Stop (will also be set if a stop on error/early termination
            condition occurs)
  1         Software Reset
  2         Stop on Error
  3         Stop on Early Termination
  4         Global Interrupt Enable Mask
  5         Stop dispatcher (stops the dispatcher from issuing more read/write
            commands)
  6-31      <Reserved>
*/

/* AVALON Memory MAP Base Addr - SEE QSYS Address Memory Map*/
#define ALTERA_MSGDMA_DMA0_CSR_BASE_ADDR 0x4000
#define ALTERA_MSGDMA_DMA1_CSR_BASE_ADDR 0x6000

#define ALTERA_MSGDMA_DMA0_CSR_STATUS_REG                ALTERA_MSGDMA_DMA0_CSR_BASE_ADDR + 0x0
#define ALTERA_MSGDMA_DMA0_CSR_CONTROL_REG               ALTERA_MSGDMA_DMA0_CSR_BASE_ADDR + 0x4
#define ALTERA_MSGDMA_DMA0_CSR_DESCRIPTOR_FILL_LEVEL_REG ALTERA_MSGDMA_DMA0_CSR_BASE_ADDR + 0x8
#define ALTERA_MSGDMA_DMA0_CSR_RESPONSE_FILL_LEVEL_REG   ALTERA_MSGDMA_DMA0_CSR_BASE_ADDR + 0xC

#define ALTERA_MSGDMA_DMA1_CSR_STATUS_REG                ALTERA_MSGDMA_DMA1_CSR_BASE_ADDR + 0x0
#define ALTERA_MSGDMA_DMA1_CSR_CONTROL_REG               ALTERA_MSGDMA_DMA1_CSR_BASE_ADDR + 0x4
#define ALTERA_MSGDMA_DMA1_CSR_DESCRIPTOR_FILL_LEVEL_REG ALTERA_MSGDMA_DMA1_CSR_BASE_ADDR + 0x8
#define ALTERA_MSGDMA_DMA1_CSR_RESPONSE_FILL_LEVEL_REG   ALTERA_MSGDMA_DMA1_CSR_BASE_ADDR + 0xC

#define ALTERA_MSGDMA_CSR_STATUS_REG                     0x0
#define ALTERA_MSGDMA_CSR_CONTROL_REG                    0x4
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_FILL_LEVEL_REG      0x8
#define ALTERA_MSGDMA_CSR_RESPONSE_FILL_LEVEL_REG        0xC
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_SEQUENCE_NUMBER_REG 0x10

/* this register only exists when the enhanced features are enabled */
#define ALTERA_MSGDMA_CSR_SEQUENCE_NUMBER_REG 0x10

/* masks for the status register bits */
#define ALTERA_MSGDMA_CSR_BUSY_MASK                           1
#define ALTERA_MSGDMA_CSR_BUSY_OFFSET                         0
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_EMPTY_MASK        (1 << 1)
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_EMPTY_OFFSET      1
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_FULL_MASK         (1 << 2)
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_BUFFER_FULL_OFFSET       2
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_EMPTY_MASK          (1 << 3)
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_EMPTY_OFFSET        3
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_FULL_MASK           (1 << 4)
#define ALTERA_MSGDMA_CSR_RESPONSE_BUFFER_FULL_OFFSET         4
#define ALTERA_MSGDMA_CSR_STOP_STATE_MASK                     (1 << 5)
#define ALTERA_MSGDMA_CSR_STOP_STATE_OFFSET                   5
#define ALTERA_MSGDMA_CSR_RESET_STATE_MASK                    (1 << 6)
#define ALTERA_MSGDMA_CSR_RESET_STATE_OFFSET                  6
#define ALTERA_MSGDMA_CSR_STOPPED_ON_ERROR_MASK               (1 << 7)
#define ALTERA_MSGDMA_CSR_STOPPED_ON_ERROR_OFFSET             7
#define ALTERA_MSGDMA_CSR_STOPPED_ON_EARLY_TERMINATION_MASK   (1 << 8)
#define ALTERA_MSGDMA_CSR_STOPPED_ON_EARLY_TERMINATION_OFFSET 8
#define ALTERA_MSGDMA_CSR_IRQ_SET_MASK                        (1 << 9)
#define ALTERA_MSGDMA_CSR_IRQ_SET_OFFSET                      9

/* masks for the control register bits */
#define ALTERA_MSGDMA_CSR_STOP_MASK                        1
#define ALTERA_MSGDMA_CSR_STOP_OFFSET                      0
#define ALTERA_MSGDMA_CSR_RESET_MASK                       (1 << 1)
#define ALTERA_MSGDMA_CSR_RESET_OFFSET                     1
#define ALTERA_MSGDMA_CSR_STOP_ON_ERROR_MASK               (1 << 2)
#define ALTERA_MSGDMA_CSR_STOP_ON_ERROR_OFFSET             2
#define ALTERA_MSGDMA_CSR_STOP_ON_EARLY_TERMINATION_MASK   (1 << 3)
#define ALTERA_MSGDMA_CSR_STOP_ON_EARLY_TERMINATION_OFFSET 3
#define ALTERA_MSGDMA_CSR_GLOBAL_INTERRUPT_MASK            (1 << 4)
#define ALTERA_MSGDMA_CSR_GLOBAL_INTERRUPT_OFFSET          4
#define ALTERA_MSGDMA_CSR_STOP_DESCRIPTORS_MASK            (1 << 5)
#define ALTERA_MSGDMA_CSR_STOP_DESCRIPTORS_OFFSET          5

/* masks for the FIFO fill levels and sequence number */
#define ALTERA_MSGDMA_CSR_READ_FILL_LEVEL_MASK         0xFFFF
#define ALTERA_MSGDMA_CSR_READ_FILL_LEVEL_OFFSET       0
#define ALTERA_MSGDMA_CSR_WRITE_FILL_LEVEL_MASK        0xFFFF0000
#define ALTERA_MSGDMA_CSR_WRITE_FILL_LEVEL_OFFSET      16
#define ALTERA_MSGDMA_CSR_RESPONSE_FILL_LEVEL_MASK     0xFFFF
#define ALTERA_MSGDMA_CSR_RESPONSE_FILL_LEVEL_OFFSET   0
#define ALTERA_MSGDMA_CSR_READ_SEQUENCE_NUMBER_MASK    0xFFFF
#define ALTERA_MSGDMA_CSR_READ_SEQUENCE_NUMBER_OFFSET  0
#define ALTERA_MSGDMA_CSR_WRITE_SEQUENCE_NUMBER_MASK   0xFFFF0000
#define ALTERA_MSGDMA_CSR_WRITE_SEQUENCE_NUMBER_OFFSET 16

#if 0
/* read/write macros for each 32 bit register of the CSR port */
#define IOWR_ALTERA_MSGDMA_CSR_STATUS(base, data) \
    PHYS_MEM_WRITE_32(BAR2_MEMORY_MAP(deviceContext, base + ALTERA_MSGDMA_CSR_STATUS_REG), data);
#define IOWR_ALTERA_MSGDMA_CSR_CONTROL(base, data) IOWR_32DIRECT(base, ALTERA_MSGDMA_CSR_CONTROL_REG, data)
#define IORD_ALTERA_MSGDMA_CSR_STATUS(base)        IORD_32DIRECT(base, ALTERA_MSGDMA_CSR_STATUS_REG)
#define IORD_ALTERA_MSGDMA_CSR_CONTROL(base)       IORD_32DIRECT(base, ALTERA_MSGDMA_CSR_CONTROL_REG)
#define IORD_ALTERA_MSGDMA_CSR_DESCRIPTOR_FILL_LEVEL(base) \
    IORD_32DIRECT(base, ALTERA_MSGDMA_CSR_DESCRIPTOR_FILL_LEVEL_REG)
#define IORD_ALTERA_MSGDMA_CSR_RESPONSE_FILL_LEVEL(base) IORD_32DIRECT(base, ALTERA_MSGDMA_CSR_RESPONSE_FILL_LEVEL_REG)
#define IORD_ALTERA_MSGDMA_CSR_SEQUENCE_NUMBER(base)     IORD_32DIRECT(base, ALTERA_MSGDMA_CSR_SEQUENCE_NUMBER_REG)
#endif

/*******************************************************************************
 *  mSGDMA Response Register Definitions
 ******************************************************************************/

/*
  The response slave port only carries the actual bytes transferred,
  error, and early termination bits.  Reading from the upper most byte
  of the 2nd register pops the response FIFO.  For proper FIFO popping
  always read the actual bytes transferred followed by the error and early
  termination bits using 'little endian' accesses.  If a big endian
  master accesses the response slave port make sure that address 0x7 is the
  last byte lane access as it's the one that pops the reponse FIFO.

  If you use a pre-fetching descriptor master in front of the dispatcher
  port then you do not need to access this response slave port.
*/

#define ALTERA_MSGDMA_RESPONSE_ACTUAL_BYTES_TRANSFERRED_REG 0x0
#define ALTERA_MSGDMA_RESPONSE_ERRORS_REG                   0x4

/* bits making up the "errors" register */
#define ALTERA_MSGDMA_RESPONSE_ERROR_MASK               0xFF
#define ALTERA_MSGDMA_RESPONSE_ERROR_OFFSET             0
#define ALTERA_MSGDMA_RESPONSE_EARLY_TERMINATION_MASK   (1 << 8)
#define ALTERA_MSGDMA_RESPONSE_EARLY_TERMINATION_OFFSET 8

/* read macros for each 32 bit register */
#define IORD_ALTERA_MSGDMA_RESPONSE_ACTUAL_BYTES_TRANSFERRED(base) \
    IORD_32DIRECT(base, ALTERA_MSGDMA_RESPONSE_ACTUAL_BYTES_TRANSFERRED_REG)
/* this read pops the response FIFO */
#define IORD_ALTERA_MSGDMA_RESPONSE_ERRORS_REG(base) IORD_32DIRECT(base, ALTERA_MSGDMA_RESPONSE_ERRORS_REG)

/*******************************************************************************
 *  mSGDMA Descriptor Register Definitions
 ******************************************************************************/

/*
  Descriptor formats:

  Standard Format:

  Offset         |    3                 2                 1                   0
  ------------------------------------------------------------------------------
   0x0           |                      Read Address[31..0]
   0x4           |                      Write Address[31..0]
   0x8           |                      Length[31..0]
   0xC           |                      Control[31..0]

  Extended Format:

Offset|   3                  2                  1                  0
 ------------------------------------------------------------------------------
 0x0  |                      Read Address[31..0]
 0x4  |                      Write Address[31..0]
 0x8  |                      Length[31..0]
 0xC  |Write Burst Count[7..0] | Read Burst Count[7..0] | Sequence Number[15..0]
 0x10 | Write Stride[15..0]           |            Read Stride[15..0]
 0x14 |                      Read Address[63..32]
 0x18 |                      Write Address[63..32]
 0x1C |                      Control[31..0]

  Note:  The control register moves from offset 0xC to 0x1C depending on the
         format used
*/

#define ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR 0x07000000

#define ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS_REG       ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x0
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS_REG      ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x4
#define ALTERA_MSGDMA_DESCRIPTOR_LENGTH_REG             ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x8
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_STANDARD_REG   ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0xC
#define ALTERA_MSGDMA_DESCRIPTOR_SEQUENCE_NUMBER_REG    ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0xC
#define ALTERA_MSGDMA_DESCRIPTOR_READ_BURST_REG         ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0xE
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_BURST_REG        ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0xF
#define ALTERA_MSGDMA_DESCRIPTOR_READ_STRIDE_REG        ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x10
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_STRIDE_REG       ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x12
#define ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS_HIGH_REG  ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x14
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS_HIGH_REG ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x18
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_ENHANCED_REG   ALTERA_MSGDMA_DESCRIPTOR_BASE_ADDR + 0x1C

/* masks and offsets for the sequence number and programmable burst counts */
#define ALTERA_MSGDMA_DESCRIPTOR_SEQUENCE_NUMBER_MASK     0xFFFF
#define ALTERA_MSGDMA_DESCRIPTOR_SEQUENCE_NUMBER_OFFSET   0
#define ALTERA_MSGDMA_DESCRIPTOR_READ_BURST_COUNT_MASK    0x00FF0000
#define ALTERA_MSGDMA_DESCRIPTOR_READ_BURST_COUNT_OFFSET  16
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_BURST_COUNT_MASK   0xFF000000
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_BURST_COUNT_OFFSET 24

/* masks and offsets for the read and write strides */
#define ALTERA_MSGDMA_DESCRIPTOR_READ_STRIDE_MASK    0xFFFF
#define ALTERA_MSGDMA_DESCRIPTOR_READ_STRIDE_OFFSET  0
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_STRIDE_MASK   0xFFFF0000
#define ALTERA_MSGDMA_DESCRIPTOR_WRITE_STRIDE_OFFSET 16

/* masks and offsets for the bits in the descriptor control field */
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSMIT_CHANNEL_MASK        0xFF
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSMIT_CHANNEL_OFFSET      0
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GENERATE_SOP_MASK            (1 << 8)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GENERATE_SOP_OFFSET          8
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GENERATE_EOP_MASK            (1 << 9)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GENERATE_EOP_OFFSET          9
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_PARK_READS_MASK              (1 << 10)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_PARK_READS_OFFSET            10
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_PARK_WRITES_MASK             (1 << 11)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_PARK_WRITES_OFFSET           11
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_END_ON_EOP_MASK              (1 << 12)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_END_ON_EOP_OFFSET            12
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSFER_COMPLETE_IRQ_MASK   (1 << 14)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_TRANSFER_COMPLETE_IRQ_OFFSET 14
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_TERMINATION_IRQ_MASK   (1 << 15)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_TERMINATION_IRQ_OFFSET 15
/* the read master will use this as the transmit error, the dispatcher will use
this to generate an interrupt if any of the error bits are asserted by the
write master */
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_ERROR_IRQ_MASK           (0xFF << 16)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_ERROR_IRQ_OFFSET         16
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_DONE_ENABLE_MASK   (1 << 24)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_EARLY_DONE_ENABLE_OFFSET 24
/* at a minimum you always have to write '1' to this bit as it commits the
descriptor to the dispatcher */
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GO_MASK   (1 << 31)
#define ALTERA_MSGDMA_DESCRIPTOR_CONTROL_GO_OFFSET 31

/* Each register is byte lane accessible so the some of the values that are
 * less than 32 bits wide are written to according to the field width.
 */
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_LENGTH(base, data) IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_LENGTH_REG, data)
/* this pushes the descriptor into the read/write FIFOs when standard descriptors
are used */
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_CONTROL_STANDARD(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_CONTROL_STANDARD_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_SEQUENCE_NUMBER(base, data) \
    IOWR_16DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_SEQUENCE_NUMBER_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_READ_BURST(base, data) \
    IOWR_8DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_READ_BURST_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_WRITE_BURST(base, data) \
    IOWR_8DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_WRITE_BURST_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_READ_STRIDE(base, data) \
    IOWR_16DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_READ_STRIDE_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_WRITE_STRIDE(base, data) \
    IOWR_16DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_WRITE_STRIDE_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS_HIGH(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_READ_ADDRESS_HIGH_REG, data)
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS_HIGH(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_WRITE_ADDRESS_HIGH_REG, data)
/* this pushes the descriptor into the read/write FIFOs when the extended
descriptors are used */
#define IOWR_ALTERA_MSGDMA_DESCRIPTOR_CONTROL_ENHANCED(base, data) \
    IOWR_32DIRECT(base, ALTERA_MSGDMA_DESCRIPTOR_CONTROL_ENHANCED_REG, data)

/*******************************************************************************
 *  mSGDMA Prefetcher Register Definitions
 ******************************************************************************/

/*
  MSGDMA Prefetcher core is an additional micro core to existing MSGDMA core which
  already consists of dispatcher, read master and write master micro core. Prefetcher
  core provides functionality to fetch a series of descriptors from memory that
  describes the required data transfers before pass them to dispatcher core for data
  transfer execution.
*/

/* AVALON Memory MAP Base Addr - SEE QSYS Address Memory Map*/
#define ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR 0x5000
#define ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR 0x7000
#define ALTERA_MSGDMA_DMA2_PREFETCHER_BASE_ADDR 0x9000

/*
 * Component : mSGDMA Prefetcher
 */
#define ALT_MSGDMA_DMA0_PREFETCHER_CONTROL_OFST                  ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR + 0x00
#define ALT_MSGDMA_DMA0_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_OFST  ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR + 0x04
#define ALT_MSGDMA_DMA0_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_OFST ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR + 0x08
#define ALT_MSGDMA_DMA0_PREFETCHER_DESCRIPTOR_POLL_FREQ_OFST     ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR + 0x0C
#define ALT_MSGDMA_DMA0_PREFETCHER_STATUS_OFST                   ALTERA_MSGDMA_DMA0_PREFETCHER_BASE_ADDR + 0x10

#define ALT_MSGDMA_DMA1_PREFETCHER_CONTROL_OFST                  ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR + 0x00
#define ALT_MSGDMA_DMA1_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_OFST  ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR + 0x04
#define ALT_MSGDMA_DMA1_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_OFST ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR + 0x08
#define ALT_MSGDMA_DMA1_PREFETCHER_DESCRIPTOR_POLL_FREQ_OFST     ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR + 0x0C
#define ALT_MSGDMA_DMA1_PREFETCHER_STATUS_OFST                   ALTERA_MSGDMA_DMA1_PREFETCHER_BASE_ADDR + 0x10

#define ALT_MSGDMA_PREFETCHER_CONTROL_REG                  0x00
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_REG  0x04
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_REG 0x08
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_REG     0x0C
#define ALT_MSGDMA_PREFETCHER_STATUS_REG                   0x10

/*
 * New MSGDMA PREFETCHER Descriptor fields. These are not prefetcher registers
 * they are in the prefetcher descriptor structs
 */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW  value. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_SET_MASK (1 << 30)
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW value. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_CLR_MASK 0xBFFFFFFF
/* The bit offset of the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW field. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_BIT_OFFSET 30
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_GET(value) (((value) & 0x40000000) >> 30)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_CTRL_OWN_BY_HW_SET(value) (((value) << 30) & 0x40000000)

/*
 * Register : control
 *
 * The control register has two defined bits.
 *
 * DESC_POLL_EN and RUN .
 *
 * Detailed description available in their individual bitfields
 *
 * Register Layout
 *
 *  Bits   | Access | Reset | Description
 * :-------|:-------|:------|:------------
 *  [0]    | R/W    | 0x0   | RUN
 *  [1]    | R/W    | 0x0   | DESC_POLL_EN
 *  [2]    | R/W1S  | 0x0   | RESET_PREFETCHER
 *  [3]    | R/W    | 0x0   | GLOBAL_INTR_EN_MASK
 *  [4]    | R/W    | 0x0   | PARK_MODE
 *  [31:5] | R      | 0x0   | RESERVED
 *
 */

/* bits making up the "control" register */

/* the RUN bit field in the control register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_SET_MASK 0x1
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_CLR_MASK 0xFFFFFFFE
/* The bit offset of the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_BIT_OFFSET 0
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_GET(value) (((value) & 0x00000001) >> 0)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_SET(value) (((value) << 0) & 0x00000001)

/* the DESC_POLL_EN bit field in the control register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN_MASK 0x2
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN_CLR_MASK 0xFFFFFFFD
/* The bit offset of the ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN_BIT_OFFSET 1
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN_GET(value) (((value) & 0x00000002) >> 1)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_DESC_POLL_EN_SET(value) (((value) << 1) & 0x00000002)

/* the RESET_PREFETCHER bit field in the control register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_CTRL_RESET register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RESET_SET_MASK 0x4
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RESET_CLR_MASK 0xFFFFFFFB
/* The bit offset of the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RESET_BIT_OFFSET 2
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RESET_GET(value) (((value) & 0x00000004) >> 2)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RESET_SET(value) (((value) << 2) & 0x00000004)

/* the GLOBAL_INTR_EN_MASK bit field in the control register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_MASK register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_SET_MASK 0x8
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_CLR_MASK 0xFFFFFFF7
/* The bit offset of the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_BIT_OFFSET 3
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_GET(value) (((value) & 0x00000008) >> 3)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_GLOBAL_INTR_EN_SET(value) (((value) << 3) & 0x00000008)

/* the PARK_MODE bit field in the control register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_SET_MASK 0x10
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_CLR_MASK 0xFFFFFFEF
/* The bit offset of the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_BIT_OFFSET 4
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_GET(value) (((value) & 0x00000010) >> 4)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_CTRL_PARK_MODE_SET(value) (((value) << 4) & 0x00000010)

/*
 * Registers : Next Descriptor Pointer Low/High
 *
 * The register has no bit fields, the 64 bits represent an address.
 *
 * Register Layout
 *
 *  Bits   | Access | Reset | Description
 * :-------|:-------|:------|:------------
 *  [31:0] | R/W    | 0x0   | NEXT_PTR_ADDR_LOW
 *  [63:32]| R/W    | 0x0   | NEXT_PTR_ADDR_HIGH
 *
 */

/* bits making up the "Next Descriptor Pointer " register */

/* the NEXT_PTR_ADDR_LOW bit field in the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_REG register field value. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_SET_MASK 0xFFFFFFFF
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field value. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_CLR_MASK 0x0
/* The bit offset of the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_BIT_OFFSET 0
/* Extracts the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG field value from a register. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_GET(value) (((value) & 0xFFFFFFFF) >> 0)
/* Produces a ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_SET(value) (((value) << 0) & 0xFFFFFFFF)

/* the NEXT_PTR_ADDR_HIGH bit field in the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_REG register field value. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_SET_MASK 0xFFFFFFFF
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field value. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_CLR_MASK 0x0
/* The bit offset of the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_BIT_OFFSET 0
/* Extracts the ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG field value from a register. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_GET(value) (((value) & 0xFFFFFFFF) >> 0)
/* Produces a ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_REG register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_SET(value) (((value) << 0) & 0xFFFFFFFF)

/*
 * Register : Descriptor Polling Frequency
 *
 * The Descriptor Polling Frequency register has one defined bit field.
 *
 * POLL_FREQ
 *
 * Detailed description available in their individual bitfields
 *
 * Register Layout
 *
 *  Bits    | Access | Reset | Description
 * :--------|:-------|:------|:------------
 *  [15:0]  | R/W    | 0x0   | POLL_FREQ
 *  [31:16] | R      | 0x0   | RESERVED
 *
 */

/* bits making up the "DESC_POLL_FREQ" register */

/* the POLL_FREQ bit field in the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ register field value. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_SET_MASK 0xFFFF
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_CLR_MASK 0xFFFF0000
/* The bit offset of the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ register field. */
#define ALT_MSGDMA_PREFETCHER_CTRL_RUN_BIT_OFFSET 0
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_GET(value) (((value) & 0x0000FFFF) >> 0)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_SET(value) (((value) << 0) & 0x0000FFFF)

/*
 * Register : Status
 *
 * The Status register has one defined bit field.
 *
 * IRQ
 *
 * Detailed description available in their individual bitfields
 *
 * Register Layout
 *
 *  Bits    | Access | Reset | Description
 * :--------|:-------|:------|:------------
 *  [0]     | R/W1C  | 0x0   | IRQ
 *  [31:1]  | R      | 0x0   | RESERVED
 *
 */

/* bits making up the "STATUS" register */

/* the IRQ bit field in the ALT_MSGDMA_PREFETCHER_STATUS register */
/* The mask used to set the ALT_MSGDMA_PREFETCHER_STATUS_IRQ register field value. */
#define ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET_MASK 0x1
/* The mask used to clear the ALT_MSGDMA_PREFETCHER_STATUS_IRQ register field value. */
#define ALT_MSGDMA_PREFETCHER_STATUS_IRQ_CLR_MASK 0xFFFFFFFE
/* The bit offset of the ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ register field. */
#define ALT_MSGDMA_PREFETCHER_STATUS_IRQ_BIT_OFFSET 0
/* Extracts the ALT_MSGDMA_PREFETCHER_CTRL_RUN field value from a register. */
#define ALT_MSGDMA_PREFETCHER_STATUS_IRQ_GET(value) (((value) & 0x00000001) >> 0)
/* Produces a ALT_MSGDMA_PREFETCHER_CTRL_RUN register field value suitable for setting the register. */
#define ALT_MSGDMA_PREFETCHER_STATUS_IRQ_SET(value) (((value) << 0) & 0x00000001)

/*****************************************************************/
/***   READ/WRITE macros for the MSGDMA PREFETCHER registers   ***/
/*****************************************************************/
/* ALT_MSGDMA_PREFETCHER_CONTROL_REG */
#define IORD_ALT_MSGDMA_PREFETCHER_CONTROL(base)       IORD_32DIRECT(base, ALT_MSGDMA_PREFETCHER_CONTROL_OFST)
#define IOWR_ALT_MSGDMA_PREFETCHER_CONTROL(base, data) IOWR_32DIRECT(base, ALT_MSGDMA_PREFETCHER_CONTROL_OFST, data)
/* ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_REG */
#define IORD_ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW(base) \
    IORD_32DIRECT(base, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_OFST)
#define IOWR_ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW(base, data) \
    IOWR_32DIRECT(base, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_LOW_OFST, data)
/* ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_REG */
#define IORD_ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH(base) \
    IORD_32DIRECT(base, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_OFST)
#define IOWR_ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH(base, data) \
    IOWR_32DIRECT(base, ALT_MSGDMA_PREFETCHER_NEXT_DESCRIPTOR_PTR_HIGH_OFST, data)
/* ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLLING_FREQ_REG */
#define IORD_ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLLING_FREQ(base) \
    IORD_32DIRECT(base, ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_OFST)
#define IOWR_ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLLING_FREQ(base, data) \
    IOWR_32DIRECT(base, ALT_MSGDMA_PREFETCHER_DESCRIPTOR_POLL_FREQ_OFST, data)
/* ALT_MSGDMA_PREFETCHER_STATUS_REG */
#define IORD_ALT_MSGDMA_PREFETCHER_STATUS(base)       IORD_32DIRECT(base, ALT_MSGDMA_PREFETCHER_STATUS_OFST)
#define IOWR_ALT_MSGDMA_PREFETCHER_STATUS(base, data) IOWR_32DIRECT(base, ALT_MSGDMA_PREFETCHER_STATUS_OFST, data)

/*******************************************************************************
 *  Driver API
 ******************************************************************************/

/* #define ALTERA_MSGDMA_USE_API */
#ifdef ALTERA_MSGDMA_USE_API
struct _DEVICE_CONTEXT;
typedef struct _DEVICE_CONTEXT DEVICE_CONTEXT, *PDEVICE_CONTEXT;
void alt_msgdma_register_callback(PDEVICE_CONTEXT deviceContext, alt_msgdma_callback callback, uint32_t control,
                                  void *context);

int alt_msgdma_standard_descriptor_async_transfer(PDEVICE_CONTEXT deviceContext, alt_msgdma_standard_descriptor *desc);

int alt_msgdma_extended_descriptor_async_transfer(PDEVICE_CONTEXT deviceContext, alt_msgdma_extended_descriptor *desc);

int alt_msgdma_construct_standard_mm_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_standard_descriptor *descriptor,
                                                      uint32_t *read_address, uint32_t *write_address, uint32_t length,
                                                      uint32_t control);

int alt_msgdma_construct_standard_st_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_standard_descriptor *descriptor,
                                                      uint32_t *write_address, uint32_t length, uint32_t control);

int alt_msgdma_construct_standard_mm_to_st_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_standard_descriptor *descriptor,
                                                      uint32_t *read_address, uint32_t length, uint32_t control);

int alt_msgdma_construct_extended_st_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_extended_descriptor *descriptor,
                                                      uint32_t *write_address, uint32_t length, uint32_t control,
                                                      uint16_t sequence_number, uint8_t write_burst_count,
                                                      uint16_t write_stride);

int alt_msgdma_construct_extended_mm_to_st_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_extended_descriptor *descriptor,
                                                      uint32_t *read_address, uint32_t length, uint32_t control,
                                                      uint16_t sequence_number, uint8_t read_burst_count,
                                                      uint16_t read_stride);

int alt_msgdma_construct_extended_mm_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                      alt_msgdma_extended_descriptor *descriptor,
                                                      uint32_t *read_address, uint32_t *write_address, uint32_t length,
                                                      uint32_t control, uint16_t sequence_number,
                                                      uint8_t read_burst_count, uint8_t write_burst_count,
                                                      uint16_t read_stride, uint16_t write_stride);

int alt_msgdma_standard_descriptor_sync_transfer(PDEVICE_CONTEXT deviceContext, alt_msgdma_standard_descriptor *desc);

int alt_msgdma_extended_descriptor_sync_transfer(PDEVICE_CONTEXT deviceContext, alt_msgdma_extended_descriptor *desc);

/***************** MSGDMA PREFETCHER PUBLIC APIs ******************/

int alt_msgdma_construct_prefetcher_standard_mm_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                                 alt_msgdma_prefetcher_standard_descriptor *descriptor,
                                                                 uint32_t read_address, uint32_t write_address,
                                                                 uint32_t length, uint32_t control);

int alt_msgdma_construct_prefetcher_standard_st_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                                 alt_msgdma_prefetcher_standard_descriptor *descriptor,
                                                                 uint32_t write_address, uint32_t length,
                                                                 uint32_t control);

int alt_msgdma_construct_prefetcher_standard_mm_to_st_descriptor(PDEVICE_CONTEXT deviceContext,
                                                                 alt_msgdma_prefetcher_standard_descriptor *descriptor,
                                                                 uint32_t read_address, uint32_t length,
                                                                 uint32_t control);

int alt_msgdma_construct_prefetcher_extended_st_to_mm_descriptor(PDEVICE_CONTEXT deviceContext,
                                                                 alt_msgdma_prefetcher_extended_descriptor *descriptor,
                                                                 uint32_t write_address_high,
                                                                 uint32_t write_address_low, uint32_t length,
                                                                 uint32_t control, uint16_t sequence_number,
                                                                 uint8_t write_burst_count, uint16_t write_stride);

int alt_msgdma_construct_prefetcher_extended_mm_to_st_descriptor(PDEVICE_CONTEXT deviceContext,
                                                                 alt_msgdma_prefetcher_extended_descriptor *descriptor,
                                                                 uint32_t read_address_high, uint32_t read_address_low,
                                                                 uint32_t length, uint32_t control,
                                                                 uint16_t sequence_number, uint8_t read_burst_count,
                                                                 uint16_t read_stride);

int alt_msgdma_construct_prefetcher_extended_mm_to_mm_descriptor(
    PDEVICE_CONTEXT deviceContext, alt_msgdma_prefetcher_extended_descriptor *descriptor, uint32_t read_address_high,
    uint32_t read_address_low, uint32_t write_address_high, uint32_t write_address_low, uint32_t length,
    uint32_t control, uint16_t sequence_number, uint8_t read_burst_count, uint8_t write_burst_count,
    uint16_t read_stride, uint16_t write_stride);

int alt_msgdma_prefetcher_add_standard_desc_to_list(alt_msgdma_prefetcher_standard_descriptor **list,
                                                    alt_msgdma_prefetcher_standard_descriptor *descriptor);

int alt_msgdma_prefetcher_add_extended_desc_to_list(alt_msgdma_prefetcher_extended_descriptor **list,
                                                    alt_msgdma_prefetcher_extended_descriptor *descriptor);

int alt_msgdma_start_prefetcher_with_std_desc_list(PDEVICE_CONTEXT deviceContext,
                                                   alt_msgdma_prefetcher_standard_descriptor *list,
                                                   uint8_t park_mode_en, uint8_t poll_en);

int alt_msgdma_start_prefetcher_with_extd_desc_list(PDEVICE_CONTEXT deviceContext,
                                                    alt_msgdma_prefetcher_extended_descriptor *list,
                                                    uint8_t park_mode_en, uint8_t poll_en);

int alt_msgdma_prefetcher_set_std_list_own_by_hw_bits(alt_msgdma_prefetcher_standard_descriptor *list);

int alt_msgdma_prefetcher_set_extd_list_own_by_hw_bits(alt_msgdma_prefetcher_extended_descriptor *list);

void alt_msgdma_init(PDEVICE_CONTEXT deviceContext, uint32_t ic_id, uint32_t irq);

/* HAL initialization macros */

/*
 * ALTERA_MSGDMA_INSTANCE is the macro used by alt_sys_init() to
 * allocate any per device memory that may be required.
 */
#define ALTERA_MSGDMA_CSR_DESCRIPTOR_SLAVE_RESPONSE_INSTANCE(name, csr_if, desc_if, resp_if, dev) \
    static alt_msgdma_dev dev = {ALT_LLIST_ENTRY,                                                 \
                                 name##_CSR_NAME,                                                 \
                                 ((uint32_t *)(csr_if##_BASE)),                                   \
                                 ((uint32_t *)(desc_if##_BASE)),                                  \
                                 ((uint32_t *)(resp_if##_BASE)),                                  \
                                 ((uint32_t *)(0)),                                               \
                                 ((uint32_t)name##_CSR_IRQ_INTERRUPT_CONTROLLER_ID),              \
                                 ((uint32_t)name##_CSR_IRQ),                                      \
                                 ((uint32_t)desc_if##_DESCRIPTOR_FIFO_DEPTH),                     \
                                 ((uint32_t)resp_if##_DESCRIPTOR_FIFO_DEPTH * 2),                 \
                                 ((void *)0x0),                                                   \
                                 ((void *)0x0),                                                   \
                                 ((uint32_t)0x0),                                                 \
                                 ((uint8_t)csr_if##_BURST_ENABLE),                                \
                                 ((uint8_t)csr_if##_BURST_WRAPPING_SUPPORT),                      \
                                 ((uint32_t)csr_if##_DATA_FIFO_DEPTH),                            \
                                 ((uint32_t)csr_if##_DATA_WIDTH),                                 \
                                 ((uint32_t)csr_if##_MAX_BURST_COUNT),                            \
                                 ((uint32_t)csr_if##_MAX_BYTE),                                   \
                                 ((uint64_t)csr_if##_MAX_STRIDE),                                 \
                                 ((uint8_t)csr_if##_PROGRAMMABLE_BURST_ENABLE),                   \
                                 ((uint8_t)csr_if##_STRIDE_ENABLE),                               \
                                 csr_if##_TRANSFER_TYPE,                                          \
                                 ((uint8_t)csr_if##_ENHANCED_FEATURES),                           \
                                 ((uint8_t)csr_if##_RESPONSE_PORT),                               \
                                 ((uint8_t)csr_if##_PREFETCHER_ENABLE)};

#define ALTERA_MSGDMA_CSR_DESCRIPTOR_SLAVE_INSTANCE(name, csr_if, desc_if, dev)      \
    static alt_msgdma_dev dev = {ALT_LLIST_ENTRY,                                    \
                                 name##_CSR_NAME,                                    \
                                 ((uint32_t *)(csr_if##_BASE)),                      \
                                 ((uint32_t *)(desc_if##_BASE)),                     \
                                 ((uint32_t *)(0)),                                  \
                                 ((uint32_t *)(0)),                                  \
                                 ((uint32_t)name##_CSR_IRQ_INTERRUPT_CONTROLLER_ID), \
                                 ((uint32_t)name##_CSR_IRQ),                         \
                                 ((uint32_t)desc_if##_DESCRIPTOR_FIFO_DEPTH),        \
                                 ((uint32_t)0x0),                                    \
                                 ((void *)0x0),                                      \
                                 ((void *)0x0),                                      \
                                 ((uint32_t)0x0),                                    \
                                 ((uint8_t)csr_if##_BURST_ENABLE),                   \
                                 ((uint8_t)csr_if##_BURST_WRAPPING_SUPPORT),         \
                                 ((uint32_t)csr_if##_DATA_FIFO_DEPTH),               \
                                 ((uint32_t)csr_if##_DATA_WIDTH),                    \
                                 ((uint32_t)csr_if##_MAX_BURST_COUNT),               \
                                 ((uint32_t)csr_if##_MAX_BYTE),                      \
                                 ((uint64_t)csr_if##_MAX_STRIDE),                    \
                                 ((uint8_t)csr_if##_PROGRAMMABLE_BURST_ENABLE),      \
                                 ((uint8_t)csr_if##_STRIDE_ENABLE),                  \
                                 csr_if##_TRANSFER_TYPE,                             \
                                 ((uint8_t)csr_if##_ENHANCED_FEATURES),              \
                                 ((uint8_t)csr_if##_RESPONSE_PORT),                  \
                                 ((uint8_t)csr_if##_PREFETCHER_ENABLE)};

/*
 * New Interface for Prefetcher 15/6/2015.
 */
#define ALTERA_MSGDMA_CSR_PREFETCHER_CSR_INSTANCE(name, csr_if, pref_if, dev)                   \
    static alt_msgdma_dev dev = {ALT_LLIST_ENTRY,                                               \
                                 name##_CSR_NAME,                                               \
                                 ((uint32_t *)(csr_if##_BASE)),                                 \
                                 ((uint32_t *)(0)),                                             \
                                 ((uint32_t *)(0)),                                             \
                                 ((uint32_t *)(pref_if##_BASE)),                                \
                                 ((uint32_t)name##_PREFETCHER_CSR_IRQ_INTERRUPT_CONTROLLER_ID), \
                                 ((uint32_t)name##_PREFETCHER_CSR_IRQ),                         \
                                 ((uint32_t)(0)),                                               \
                                 ((uint32_t)0x0),                                               \
                                 ((void *)0x0),                                                 \
                                 ((void *)0x0),                                                 \
                                 ((uint32_t)0x0),                                               \
                                 ((uint8_t)csr_if##_BURST_ENABLE),                              \
                                 ((uint8_t)csr_if##_BURST_WRAPPING_SUPPORT),                    \
                                 ((uint32_t)csr_if##_DATA_FIFO_DEPTH),                          \
                                 ((uint32_t)csr_if##_DATA_WIDTH),                               \
                                 ((uint32_t)csr_if##_MAX_BURST_COUNT),                          \
                                 ((uint32_t)csr_if##_MAX_BYTE),                                 \
                                 ((uint64_t)csr_if##_MAX_STRIDE),                               \
                                 ((uint8_t)csr_if##_PROGRAMMABLE_BURST_ENABLE),                 \
                                 ((uint8_t)csr_if##_STRIDE_ENABLE),                             \
                                 csr_if##_TRANSFER_TYPE,                                        \
                                 ((uint8_t)csr_if##_ENHANCED_FEATURES),                         \
                                 ((uint8_t)csr_if##_RESPONSE_PORT),                             \
                                 ((uint8_t)csr_if##_PREFETCHER_ENABLE)};

/*
 * The macro ALTERA_MSGDMA_INIT is called by the auto-generated function
 * alt_sys_init() to initialize a given device instance.
 */
#define ALTERA_MSGDMA_INIT(name, dev) alt_msgdma_init(&dev, dev.irq_controller_ID, dev.irq_ID);
#endif  /* ALTERA_MSGDMA_USE_API */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ALTERA_MSGDMA_H */
