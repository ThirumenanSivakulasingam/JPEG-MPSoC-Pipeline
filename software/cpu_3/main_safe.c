#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "altera_avalon_fifo_util.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "config.h"
#include "avalon_fifo.h"

#define RECV_PAIR() ({ \
    UINT32 word = altera_avalon_fifo_read_backpressure(FIFO_2_3_OUT_BASE); \
    INT16 val1 = (INT16)(word & 0xFFFF); \
    INT16 val2 = (INT16)((word >> 16) & 0xFFFF); \
    pair_buf[0] = val1; pair_buf[1] = val2; \
})
#define SEND_PAIR(val1, val2) \
    FIFO_WRITE_BLOCK(FIFO_3_4_IN_BASE, FIFO_3_4_IN_CSR_BASE, ((UINT32)(val1) & 0xFFFF) | (((UINT32)(val2) & 0xFFFF) << 16))

static INT16 pair_buf[2];

volatile UINT32 g_stage_status = 0;
volatile UINT32 g_stage_count  = 0;

static const UINT8 zigzag[64] = {0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63};
static UINT8  Lqt[64], Cqt[64];
static UINT16 ILqt[64], ICqt[64];
static INT16  Temp[64];

static UINT16 DSP_Div(UINT32 n, UINT32 d) { UINT16 i; d <<= 15; for (i = 16; i > 0; i--) { if (n > d) { n -= d; n <<= 1; n++; } else n <<= 1; } return (UINT16)n; }

static void init_quant(UINT32 qf)
{
    UINT16 i, idx; UINT32 v;
    static const UINT8 lum[64] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
    static const UINT8 chr[64] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
    for (i = 0; i < 64; i++) {
        idx = zigzag[i];
        v = lum[i] * qf; v = (v + 0x200) >> 10; if (!v) v = 1; else if (v > 255) v = 255;
        Lqt[idx] = (UINT8)v; ILqt[i] = DSP_Div(0x8000, v);
        v = chr[i] * qf; v = (v + 0x200) >> 10; if (!v) v = 1; else if (v > 255) v = 255;
        Cqt[idx] = (UINT8)v; ICqt[i] = DSP_Div(0x8000, v);
    }
}

static void quant(UINT16 *qt)
{
    INT16 i; INT32 v;
    for (i = 0; i < 64; i += 2)
    {
        RECV_PAIR();
        Temp[zigzag[i]] = (INT16)(((INT32)pair_buf[0] * (INT32)qt[i] + 0x4000) >> 15);
        Temp[zigzag[i + 1]] = (INT16)(((INT32)pair_buf[1] * (INT32)qt[i + 1] + 0x4000) >> 15);
    }
    for (i = 0; i < 64; i += 2) SEND_PAIR(Temp[i], Temp[i+1]);
}

int main(void)
{
    UINT16 i;
    UINT32 num_mcus, qf, processed_mcus, total_mcus, percent, last_reported_percent;
    UINT32 ticks_per_sec, stage_start_ticks, stage_elapsed_ticks, stage_elapsed_ms;
    RECV_PAIR();
    num_mcus = ((UINT32)pair_buf[0]) | (((UINT32)pair_buf[1]) << 16);
    RECV_PAIR();
    qf = ((UINT32)pair_buf[0]) | (((UINT32)pair_buf[1]) << 16);
    total_mcus = num_mcus;
    processed_mcus = 0;

    g_stage_status = 1;
    g_stage_count = 0;
    printf("started\n");

    init_quant(qf);
    for (i = 0; i < 64; i += 2) SEND_PAIR(Lqt[i], Lqt[i+1]);
    for (i = 0; i < 64; i += 2) SEND_PAIR(Cqt[i], Cqt[i+1]);
    SEND_PAIR(num_mcus & 0xFFFF, (num_mcus >> 16) & 0xFFFF);
    g_stage_status = 2;
    printf("forwarded quant tables and header: %u MCUs, q=%u\n", (unsigned)total_mcus, (unsigned)qf);

    g_stage_status = 3;
    printf("processing MCU blocks\n");
    ticks_per_sec = alt_ticks_per_second();
    if (ticks_per_sec == 0) ticks_per_sec = 1000u;
    stage_start_ticks = alt_nticks();
    last_reported_percent = 0;
    for (; num_mcus > 0; num_mcus--)
    {
        quant(ILqt); quant(ICqt); quant(ICqt);
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
