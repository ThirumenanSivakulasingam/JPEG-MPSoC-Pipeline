#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "altera_avalon_fifo_util.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "avalon_fifo.h"

#define RECV_PAIR() ({ \
    UINT32 word = altera_avalon_fifo_read_backpressure(FIFO_0_1_1_OUT_BASE); \
    INT16 val1 = (INT16)(word & 0xFFFF); \
    INT16 val2 = (INT16)((word >> 16) & 0xFFFF); \
    pair_buf[0] = val1; pair_buf[1] = val2; \
})
#define SEND_PAIR(val1, val2) \
    FIFO_WRITE_BLOCK(FIFO_1_2_IN_BASE, FIFO_1_2_IN_CSR_BASE, ((UINT32)(val1) & 0xFFFF) | (((UINT32)(val2) & 0xFFFF) << 16))

static INT16 pair_buf[2];

volatile UINT32 g_stage_status = 0;
volatile UINT32 g_stage_count  = 0;

int main(void)
{
    INT16  i, val;
    UINT32 num_mcus, quality_factor, processed_mcus, total_mcus, percent, last_reported_percent;
    UINT32 ticks_per_sec, stage_start_ticks, stage_elapsed_ticks, stage_elapsed_ms;

    RECV_PAIR();
    num_mcus = ((UINT32)pair_buf[0]) | (((UINT32)pair_buf[1]) << 16);
    RECV_PAIR();
    quality_factor = ((UINT32)pair_buf[0]) | (((UINT32)pair_buf[1]) << 16);
    total_mcus     = num_mcus;
    processed_mcus = 0;

    g_stage_status = 1;
    g_stage_count = 0;
    printf("started\n");

    SEND_PAIR(num_mcus & 0xFFFF, (num_mcus >> 16) & 0xFFFF);
    SEND_PAIR(quality_factor & 0xFFFF, (quality_factor >> 16) & 0xFFFF);
    g_stage_status = 2;
    printf("forwarded header: %u MCUs, q=%u\n", (unsigned)total_mcus, (unsigned)quality_factor);

    g_stage_status = 3;
    printf("processing MCU blocks\n");
    ticks_per_sec = alt_ticks_per_second();
    if (ticks_per_sec == 0) ticks_per_sec = 1000u;
    stage_start_ticks = alt_nticks();
    last_reported_percent = 0;
    for (; num_mcus > 0; num_mcus--)
    {
        for (i = 0; i < 64; i += 2) { RECV_PAIR(); SEND_PAIR(pair_buf[0] - 128, pair_buf[1] - 128); }
        for (i = 0; i < 64; i += 2) { RECV_PAIR(); SEND_PAIR(pair_buf[0] - 128, pair_buf[1] - 128); }
        for (i = 0; i < 64; i += 2) { RECV_PAIR(); SEND_PAIR(pair_buf[0] - 128, pair_buf[1] - 128); }
        processed_mcus++;
        g_stage_count = processed_mcus;
        if (total_mcus > 0)
        {
            percent = (processed_mcus * 100u) / total_mcus;
            percent = (percent / 25u) * 25u;
            if (percent >= last_reported_percent + 25u)
            {
                printf("%u%% (%u/%u MCUs)\n",
                       (unsigned)percent,
                       (unsigned)processed_mcus,
                       (unsigned)total_mcus);
                last_reported_percent = percent;
            }
        }
    }

    g_stage_status = 4;
    printf("finished\n");
    stage_elapsed_ticks = alt_nticks() - stage_start_ticks;
    stage_elapsed_ms = (stage_elapsed_ticks * 1000u) / ticks_per_sec;
    if (stage_elapsed_ms == 0) stage_elapsed_ms = 1u;
    printf("METRICS elapsed_ms=%u mcus=%u\n",
           (unsigned)stage_elapsed_ms,
           (unsigned)processed_mcus);
    for (;;)
        ;
}
