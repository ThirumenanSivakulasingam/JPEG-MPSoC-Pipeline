#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "altera_avalon_fifo_util.h"
#include "system.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "avalon_fifo.h"

#define RECV_PAIR() ({ \
    UINT32 word = altera_avalon_fifo_read_backpressure(FIFO_1_2_OUT_BASE); \
    INT16 val1 = (INT16)(word & 0xFFFF); \
    INT16 val2 = (INT16)((word >> 16) & 0xFFFF); \
    pair_buf[0] = val1; pair_buf[1] = val2; \
})
#define SEND_PAIR(val1, val2) \
    FIFO_WRITE_BLOCK(FIFO_2_3_IN_BASE, FIFO_2_3_IN_CSR_BASE, ((UINT32)(val1) & 0xFFFF) | (((UINT32)(val2) & 0xFFFF) << 16))

static INT16 pair_buf[2];

volatile UINT32 g_stage_status = 0;
volatile UINT32 g_stage_count  = 0;

static INT16 data[64];

static void dct_block_software(void)
{
    UINT16 i;
    INT32 x0, x1, x2, x3, x4, x5, x6, x7, x8;
    static const UINT16 c1 = 1420, c2 = 1338, c3 = 1204, c5 = 805, c6 = 554, c7 = 283;
    static const UINT16 s1 = 3, s2 = 10, s3 = 13;
    INT16 *dp = data;

    for (i = 8; i > 0; i--)
    {
        x8 = dp[0] + dp[7]; x0 = dp[0] - dp[7]; x7 = dp[1] + dp[6]; x1 = dp[1] - dp[6];
        x6 = dp[2] + dp[5]; x2 = dp[2] - dp[5]; x5 = dp[3] + dp[4]; x3 = dp[3] - dp[4];
        x4 = x8 + x5; x8 -= x5; x5 = x7 + x6; x7 -= x6;
        dp[0] = (INT16)(x4 + x5); dp[4] = (INT16)(x4 - x5);
        dp[2] = (INT16)((x8 * c2 + x7 * c6) >> s2); dp[6] = (INT16)((x8 * c6 - x7 * c2) >> s2);
        dp[7] = (INT16)((x0 * c7 - x1 * c5 + x2 * c3 - x3 * c1) >> s2); dp[5] = (INT16)((x0 * c5 - x1 * c1 + x2 * c7 + x3 * c3) >> s2);
        dp[3] = (INT16)((x0 * c3 - x1 * c7 - x2 * c1 - x3 * c5) >> s2); dp[1] = (INT16)((x0 * c1 + x1 * c3 + x2 * c5 + x3 * c7) >> s2);
        dp += 8;
    }
    dp -= 64;
    for (i = 8; i > 0; i--)
    {
        x8 = dp[0] + dp[56]; x0 = dp[0] - dp[56]; x7 = dp[8] + dp[48]; x1 = dp[8] - dp[48];
        x6 = dp[16] + dp[40]; x2 = dp[16] - dp[40]; x5 = dp[24] + dp[32]; x3 = dp[24] - dp[32];
        x4 = x8 + x5; x8 -= x5; x5 = x7 + x6; x7 -= x6;
        dp[0] = (INT16)((x4 + x5) >> s1); dp[32] = (INT16)((x4 - x5) >> s1);
        dp[16] = (INT16)((x8 * c2 + x7 * c6) >> s3); dp[48] = (INT16)((x8 * c6 - x7 * c2) >> s3);
        dp[56] = (INT16)((x0 * c7 - x1 * c5 + x2 * c3 - x3 * c1) >> s3); dp[40] = (INT16)((x0 * c5 - x1 * c1 + x2 * c7 + x3 * c3) >> s3);
        dp[24] = (INT16)((x0 * c3 - x1 * c7 - x2 * c1 - x3 * c5) >> s3); dp[8] = (INT16)((x0 * c1 + x1 * c3 + x2 * c5 + x3 * c7) >> s3);
        dp++;
    }
}

#if defined(ALT_CI_JPEG_DCT_0_N) && defined(USE_CUSTOM_DCT)
static UINT8 g_use_custom_dct = 1;

static void dct_ci_load_pair(UINT8 slot, UINT32 packed)
{
    switch (slot)
    {
        case 0u: (void)ALT_CI_JPEG_DCT_0(0, packed, 0); break;
        case 1u: (void)ALT_CI_JPEG_DCT_0(1, packed, 0); break;
        case 2u: (void)ALT_CI_JPEG_DCT_0(2, packed, 0); break;
        case 3u: (void)ALT_CI_JPEG_DCT_0(3, packed, 0); break;
        default: break;
    }
}

static void dct_ci_run_row(void)
{
    (void)ALT_CI_JPEG_DCT_0(4, 0, 0);
}

static void dct_ci_run_col(void)
{
    (void)ALT_CI_JPEG_DCT_0(5, 0, 0);
}

static INT16 dct_ci_read_value(UINT8 slot)
{
    switch (slot)
    {
        case 0u: return (INT16)ALT_CI_JPEG_DCT_0(8, 0, 0);
        case 1u: return (INT16)ALT_CI_JPEG_DCT_0(9, 0, 0);
        case 2u: return (INT16)ALT_CI_JPEG_DCT_0(10, 0, 0);
        case 3u: return (INT16)ALT_CI_JPEG_DCT_0(11, 0, 0);
        case 4u: return (INT16)ALT_CI_JPEG_DCT_0(12, 0, 0);
        case 5u: return (INT16)ALT_CI_JPEG_DCT_0(13, 0, 0);
        case 6u: return (INT16)ALT_CI_JPEG_DCT_0(14, 0, 0);
        case 7u: return (INT16)ALT_CI_JPEG_DCT_0(15, 0, 0);
        default: return 0;
    }
}

static void dct_1d_ci(INT16 *vec, UINT8 do_column)
{
    UINT16 i;
    UINT32 packed;

    for (i = 0; i < 8; i += 2)
    {
        packed = ((UINT32)(UINT16)vec[i]) | (((UINT32)(UINT16)vec[i + 1]) << 16);
        dct_ci_load_pair(i / 2u, packed);
    }

    if (do_column)
        dct_ci_run_col();
    else
        dct_ci_run_row();

    for (i = 0; i < 8; ++i)
        vec[i] = dct_ci_read_value(i);
}

static void dct_block_custom(void)
{
    UINT16 row, col;
    INT16 column[8];

    for (row = 0; row < 8; ++row)
        dct_1d_ci(&data[row * 8], 0u);

    for (col = 0; col < 8; ++col)
    {
        for (row = 0; row < 8; ++row)
            column[row] = data[row * 8 + col];

        dct_1d_ci(column, 1u);

        for (row = 0; row < 8; ++row)
            data[row * 8 + col] = column[row];
    }
}

static void dct_block_software_ref(INT16 *block)
{
    UINT16 i;
    for (i = 0; i < 64; ++i)
        data[i] = block[i];
    dct_block_software();
    for (i = 0; i < 64; ++i)
        block[i] = data[i];
}

static void dct_block_custom_ref(INT16 *block)
{
    UINT16 i;
    for (i = 0; i < 64; ++i)
        data[i] = block[i];
    dct_block_custom();
    for (i = 0; i < 64; ++i)
        block[i] = data[i];
}

static int custom_dct_selftest(void)
{
    static const INT16 test_vectors[3][64] = {
        {
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0
        },
        {
           -32, -31, -30, -29, -28, -27, -26, -25,
           -24, -23, -22, -21, -20, -19, -18, -17,
           -16, -15, -14, -13, -12, -11, -10,  -9,
            -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,
             0,   1,   2,   3,   4,   5,   6,   7,
             8,   9,  10,  11,  12,  13,  14,  15,
            16,  17,  18,  19,  20,  21,  22,  23,
            24,  25,  26,  27,  28,  29,  30,  31
        },
        {
            40, -40,  40, -40,  40, -40,  40, -40,
           -40,  40, -40,  40, -40,  40, -40,  40,
            40, -40,  40, -40,  40, -40,  40, -40,
           -40,  40, -40,  40, -40,  40, -40,  40,
            40, -40,  40, -40,  40, -40,  40, -40,
           -40,  40, -40,  40, -40,  40, -40,  40,
            40, -40,  40, -40,  40, -40,  40, -40,
           -40,  40, -40,  40, -40,  40, -40,  40
        }
    };
    INT16 sw_block[64];
    INT16 hw_block[64];
    UINT16 test_idx, i;

    for (test_idx = 0; test_idx < 3; ++test_idx)
    {
        for (i = 0; i < 64; ++i)
        {
            sw_block[i] = test_vectors[test_idx][i];
            hw_block[i] = test_vectors[test_idx][i];
        }

        dct_block_software_ref(sw_block);
        dct_block_custom_ref(hw_block);

        for (i = 0; i < 64; ++i)
        {
            if (sw_block[i] != hw_block[i])
                return 0;
        }
    }

    return 1;
}
#endif

static void DCT(void)
{
    UINT16 i;

    for (i = 0; i < 64; i += 2) { RECV_PAIR(); data[i] = pair_buf[0]; data[i+1] = pair_buf[1]; }

#if defined(ALT_CI_JPEG_DCT_0_N) && defined(USE_CUSTOM_DCT)
    if (g_use_custom_dct)
        dct_block_custom();
    else
        dct_block_software();
#else
    dct_block_software();
#endif

    for (i = 0; i < 64; i += 2) SEND_PAIR(data[i], data[i+1]);
}

int main(void)
{
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
#if defined(ALT_CI_JPEG_DCT_0_N) && defined(USE_CUSTOM_DCT)
    if (custom_dct_selftest())
    {
        g_use_custom_dct = 1;
        printf("custom DCT self-test PASS\n");
        printf("using custom DCT accelerator\n");
    }
    else
    {
        g_use_custom_dct = 0;
        printf("custom DCT self-test FAIL\n");
        printf("falling back to software DCT\n");
    }
#else
    printf("using software DCT\n");
#endif

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
        DCT(); DCT(); DCT();
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
