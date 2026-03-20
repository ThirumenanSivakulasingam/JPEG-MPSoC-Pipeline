/*
 * cpu_2 / jpeg_addrs.h
 * Thin compatibility layer over the BSP-generated hardware map.
 */
#ifndef CPU2_JPEG_ADDRS_H
#define CPU2_JPEG_ADDRS_H

#include "system.h"

#ifndef ONCHIP_MEM_2_BASE
#define ONCHIP_MEM_2_BASE 0x00008000
#endif

#ifndef JTAG_UART_2_BASE
#define JTAG_UART_2_BASE 0x00011040
#endif

#ifndef TIMER_0_BASE
#define TIMER_0_BASE 0x04041060
#endif

#ifndef FIFO_1_2_OUT_BASE
#ifdef FIFO_1_2_BASE
#define FIFO_1_2_OUT_BASE FIFO_1_2_BASE
#else
#define FIFO_1_2_OUT_BASE 0x0001104C
#endif
#endif

#ifndef FIFO_2_3_IN_BASE
#define FIFO_2_3_IN_BASE 0x00011048
#endif

#ifndef FIFO_2_3_IN_CSR_BASE
#define FIFO_2_3_IN_CSR_BASE 0x00011000
#endif

#endif /* CPU2_JPEG_ADDRS_H */
