/*
 * cpu_5 / jpeg_addrs.h
 * Thin compatibility layer over the BSP-generated hardware map.
 */
#ifndef CPU5_JPEG_ADDRS_H
#define CPU5_JPEG_ADDRS_H

#include "system.h"

#ifndef ONCHIP_MEM_5_BASE
#define ONCHIP_MEM_5_BASE 0x00008000
#endif

#ifndef JTAG_UART_5_BASE
#define JTAG_UART_5_BASE 0x00011020
#endif

#ifndef TIMER_0_BASE
#define TIMER_0_BASE 0x04041060
#endif

#ifndef SDRAM_0_BASE
#define SDRAM_0_BASE 0x02000000
#endif

#ifndef FIFO_4_5_OUT_BASE
#ifdef FIFO_4_5_BASE
#define FIFO_4_5_OUT_BASE FIFO_4_5_BASE
#else
#define FIFO_4_5_OUT_BASE 0x00011028
#endif
#endif

#endif /* CPU5_JPEG_ADDRS_H */
