/*
 * avalon_fifo.h
 *
 * Portable FIFO read/write macros for Nios II / Avalon-MM FIFO IP.
 *
 * The altera_avalon_fifo IP exposes two register interfaces per port:
 *   - DATA register  at base + 0x00  (32-bit read or write)
 *   - CSR  register  at base + 0x04  (status / event flags)
 *
 * CSR bit layout (Altera Avalon FIFO):
 *   bit 0  : FULL
 *   bit 1  : EMPTY
 *   bit 2  : ALMOST_FULL
 *   bit 3  : ALMOST_EMPTY
 *   bit 8  : OVERFLOW  (write-1-to-clear)
 *   bit 9  : UNDERFLOW (write-1-to-clear)
 *
 * Usage:
 *   FIFO_WRITE(FIFO_0_1_1_IN_BASE, value);
 *   value = FIFO_READ(FIFO_1_2_OUT_BASE);
 *
 *   while (FIFO_FULL(FIFO_0_1_1_IN_CSR_BASE));  // spin until space
 *   while (FIFO_EMPTY(FIFO_1_2_OUT_BASE + 4));   // spin until data
 */

#ifndef AVALON_FIFO_H
#define AVALON_FIFO_H

#include <io.h>   /* IORD_32DIRECT / IOWR_32DIRECT from Altera HAL */

/* --- Low-level 32-bit Avalon accessors ----------------------------------- */
#define FIFO_WRITE(base, val)   IOWR_32DIRECT((base), 0, (UINT32)(val))
#define FIFO_READ(base)         IORD_32DIRECT((base), 0)
#define FIFO_CSR_READ(csr)      IORD_32DIRECT((csr),  0)

/* --- Status helpers ------------------------------------------------------ */
#define FIFO_FULL(csr_base)     (FIFO_CSR_READ(csr_base) & 0x1)
#define FIFO_EMPTY(csr_base)    (FIFO_CSR_READ(csr_base) & 0x2)

/* --- Blocking helpers ---------------------------------------------------- */
/* Write one 32-bit word — spins while FIFO is full */
#define FIFO_WRITE_BLOCK(data_base, csr_base, val) \
    do { \
        while (FIFO_FULL(csr_base)); \
        FIFO_WRITE(data_base, val); \
    } while (0)

/* Read one 32-bit word — spins while FIFO is empty */
#define FIFO_READ_BLOCK(data_base, csr_base) \
    ({ \
        while (FIFO_EMPTY(csr_base)); \
        (UINT32)FIFO_READ(data_base); \
    })

#endif /* AVALON_FIFO_H */
