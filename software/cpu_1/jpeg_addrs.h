/*
 * cpu_1 / jpeg_addrs.h
 * Thin compatibility layer over the BSP-generated hardware map.
 */
#ifndef CPU1_JPEG_ADDRS_H
#define CPU1_JPEG_ADDRS_H

#include "system.h"

#ifndef ONCHIP_MEM_1_BASE
#define ONCHIP_MEM_1_BASE 0x00008000
#endif

#ifndef JTAG_UART_1_BASE
#define JTAG_UART_1_BASE 0x00011020
#endif

#ifndef TIMER_0_BASE
#define TIMER_0_BASE 0x04041060
#endif

#ifndef FIFO_0_1_1_OUT_BASE
#ifdef FIFO_0_1_1_BASE
#define FIFO_0_1_1_OUT_BASE FIFO_0_1_1_BASE
#else
#define FIFO_0_1_1_OUT_BASE 0x00011028
#endif
#endif

#ifndef FIFO_1_2_IN_BASE
#define FIFO_1_2_IN_BASE 0x040410A0
#endif

#ifndef FIFO_1_2_IN_CSR_BASE
#define FIFO_1_2_IN_CSR_BASE 0x04041000
#endif

#endif /* CPU1_JPEG_ADDRS_H */
