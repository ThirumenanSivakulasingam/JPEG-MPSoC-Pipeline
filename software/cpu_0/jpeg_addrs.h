/*
 * cpu_0 / jpeg_addrs.h
 * Thin compatibility layer over the BSP-generated hardware map.
 */
#ifndef CPU0_JPEG_ADDRS_H
#define CPU0_JPEG_ADDRS_H

#include "system.h"

#ifndef SDRAM_0_BASE
#define SDRAM_0_BASE 0x02000000
#endif

#ifndef ONCHIP_MEM_0_BASE
#define ONCHIP_MEM_0_BASE 0x04020000
#endif

#ifndef JTAG_UART_0_BASE
#define JTAG_UART_0_BASE 0x04041098
#endif

#ifndef TIMER_0_BASE
#define TIMER_0_BASE 0x04041060
#endif

#ifndef FIFO_0_1_1_IN_BASE
#define FIFO_0_1_1_IN_BASE 0x040410BC
#endif

#ifndef FIFO_0_1_1_IN_CSR_BASE
#define FIFO_0_1_1_IN_CSR_BASE 0x04041040
#endif

#ifndef FIFO_0_1_5_IN_BASE
#define FIFO_0_1_5_IN_BASE 0x040410A4
#endif

#ifndef FIFO_0_1_5_IN_CSR_BASE
#define FIFO_0_1_5_IN_CSR_BASE 0x04041020
#endif

#ifndef FIFO_1_2_IN_BASE
#define FIFO_1_2_IN_BASE 0x040410A0
#endif

#ifndef FIFO_1_2_IN_CSR_BASE
#define FIFO_1_2_IN_CSR_BASE 0x04041000
#endif

#endif /* CPU0_JPEG_ADDRS_H */
