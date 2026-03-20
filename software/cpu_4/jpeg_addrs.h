/*
 * cpu_4 / jpeg_addrs.h
 * Thin compatibility layer over the BSP-generated hardware map.
 */
#ifndef CPU4_JPEG_ADDRS_H
#define CPU4_JPEG_ADDRS_H

#include "system.h"

#ifndef ONCHIP_MEM_4_BASE
#define ONCHIP_MEM_4_BASE 0x00008000
#endif

#ifndef JTAG_UART_4_BASE
#define JTAG_UART_4_BASE 0x00011020
#endif

#ifndef TIMER_0_BASE
#define TIMER_0_BASE 0x04041060
#endif

#ifndef FIFO_3_4_OUT_BASE
#ifdef FIFO_3_4_BASE
#define FIFO_3_4_OUT_BASE FIFO_3_4_BASE
#else
#define FIFO_3_4_OUT_BASE 0x0001102C
#endif
#endif

#ifndef FIFO_4_5_IN_BASE
#define FIFO_4_5_IN_BASE 0x00011028
#endif

#ifndef FIFO_4_5_IN_CSR_BASE
#define FIFO_4_5_IN_CSR_BASE 0x00011000
#endif

#ifndef FIFO_0_1_5_OUT_BASE
#define FIFO_0_1_5_OUT_BASE 0x040410AC
#endif

#endif /* CPU4_JPEG_ADDRS_H */
