#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "altera_avalon_fifo_util.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "avalon_fifo.h"

#define JPEG_META_BASE     ((UINT32)0x02600000)
#define JPEG_SIZE_ADDR     (JPEG_META_BASE + 0x00)
#define JPEG_PROGRESS_ADDR (JPEG_META_BASE + 0x04)
#define JPEG_STATUS_ADDR   (JPEG_META_BASE + 0x08)
#define JPEG_ERROR_ADDR    (JPEG_META_BASE + 0x0C)
#define JPEG_DATA_START    (JPEG_META_BASE + 0x100)
#define JPEG_MAX_SIZE      ((UINT32)0x00200000)

#define JPEG_STATUS_IDLE    0u
#define JPEG_STATUS_RUNNING 1u
#define JPEG_STATUS_HEADER  2u
#define JPEG_STATUS_STREAM  3u
#define JPEG_STATUS_DONE    4u
#define JPEG_STATUS_ERROR   5u

#define RECV() ((UINT8)altera_avalon_fifo_read_backpressure(FIFO_4_5_OUT_BASE))

static void put_hex_byte(UINT8 value)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    putchar(hex_digits[(value >> 4) & 0x0F]);
    putchar(hex_digits[value & 0x0F]);
}

static void dump_jpeg_uart(UINT32 jpeg_size)
{
    UINT32 offset = 0;
    UINT32 line_end;

    printf("JPEG_DUMP_BEGIN size=%u\n", (unsigned)jpeg_size);
    while (offset < jpeg_size)
    {
        line_end = offset + 32u;
        if (line_end > jpeg_size) line_end = jpeg_size;
        printf("JPEG_DUMP ");
        while (offset < line_end)
        {
            put_hex_byte(IORD_8DIRECT(JPEG_DATA_START + offset, 0));
            offset++;
        }
        putchar('\n');
    }
    printf("JPEG_DUMP_END size=%u\n", (unsigned)jpeg_size);
}

int main(void)
{
    UINT8  byte;
    UINT8  prev_byte;
    UINT32 jpeg_size;
    UINT32 write_addr;
    UINT32 total_mcus;
    UINT32 estimated_total;
    UINT32 percent;
    UINT32 last_percent;
    UINT32 ticks_per_sec;
    UINT32 stream_start_ticks;
    UINT32 stream_elapsed_ticks;
    UINT32 stream_elapsed_ms;

    IOWR_32DIRECT(JPEG_SIZE_ADDR, 0, 0);
    IOWR_32DIRECT(JPEG_PROGRESS_ADDR, 0, 0);
    IOWR_32DIRECT(JPEG_STATUS_ADDR, 0, JPEG_STATUS_RUNNING);
    IOWR_32DIRECT(JPEG_ERROR_ADDR, 0, 0);
    printf("started and waiting for the JPEG stream\n");

    total_mcus  = ((UINT32)RECV()) << 24;
    total_mcus |= ((UINT32)RECV()) << 16;
    total_mcus |= ((UINT32)RECV()) << 8;
    total_mcus |= ((UINT32)RECV());
    IOWR_32DIRECT(JPEG_STATUS_ADDR, 0, JPEG_STATUS_HEADER);
    printf("received stream header: %u MCUs\n", (unsigned)total_mcus);

    estimated_total = total_mcus * 100;
    if (estimated_total < 1) estimated_total = 1;

    write_addr   = JPEG_DATA_START;
    jpeg_size    = 0;
    prev_byte    = 0;
    last_percent = 0;
    ticks_per_sec = alt_ticks_per_second();
    if (ticks_per_sec == 0) ticks_per_sec = 1000u;
    stream_start_ticks = alt_nticks();

    while (1)
    {
        byte = RECV();
        if (jpeg_size == 0)
        {
            IOWR_32DIRECT(JPEG_STATUS_ADDR, 0, JPEG_STATUS_STREAM);
            printf("writing JPEG bytes to SDRAM\n");
        }

        IOWR_8DIRECT(write_addr, 0, byte);
        write_addr++;
        jpeg_size++;

        if ((jpeg_size & 0xFF) == 0)
        {
            if (jpeg_size < estimated_total) percent = (jpeg_size * 100) / estimated_total;
            else percent = 99;
            percent = (percent / 10) * 10;
            if (percent >= last_percent + 10)
            {
                IOWR_32DIRECT(JPEG_PROGRESS_ADDR, 0, percent);
                printf("progress %u%%\n", (unsigned)percent);
                last_percent = percent;
            }
        }

        if (prev_byte == 0xFF && byte == 0xD9) break;

        if (jpeg_size >= JPEG_MAX_SIZE)
        {
            IOWR_32DIRECT(JPEG_ERROR_ADDR, 0, 1);
            IOWR_32DIRECT(JPEG_STATUS_ADDR, 0, JPEG_STATUS_ERROR);
            printf("ERROR buffer limit reached\n");
            break;
        }

        prev_byte = byte;
    }

    IOWR_32DIRECT(JPEG_SIZE_ADDR, 0, jpeg_size);
    IOWR_32DIRECT(JPEG_PROGRESS_ADDR, 0, 100);
    if (jpeg_size < JPEG_MAX_SIZE) IOWR_32DIRECT(JPEG_STATUS_ADDR, 0, JPEG_STATUS_DONE);
    if (jpeg_size < JPEG_MAX_SIZE)
    {
        printf("DONE size=%u bytes\n", (unsigned)jpeg_size);
        stream_elapsed_ticks = alt_nticks() - stream_start_ticks;
        stream_elapsed_ms = (stream_elapsed_ticks * 1000u) / ticks_per_sec;
        if (stream_elapsed_ms == 0) stream_elapsed_ms = 1u;
        printf("METRICS elapsed_ms=%u bytes=%u mcus=%u\n",
               (unsigned)stream_elapsed_ms,
               (unsigned)jpeg_size,
               (unsigned)total_mcus);
        dump_jpeg_uart(jpeg_size);
    }

    for (;;)
        ;
}
