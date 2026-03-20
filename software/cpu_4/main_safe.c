#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "altera_avalon_fifo_util.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "avalon_fifo.h"
#include "huffdata.h"
#include "markdata.h"

#define RECV_META()  ((UINT32)altera_avalon_fifo_read_backpressure(FIFO_0_1_5_OUT_BASE))
#define RECV_WORD()  ((UINT32)altera_avalon_fifo_read_backpressure(FIFO_3_4_OUT_BASE))
#define SEND_BYTE(b) FIFO_WRITE_BLOCK(FIFO_4_5_IN_BASE, FIFO_4_5_IN_CSR_BASE, (UINT32)(UINT8)(b))

static INT16 quant_buf[2];
static UINT8 quant_buf_index = 2;

static void recv_quant_pair(INT16 *val1, INT16 *val2)
{
    UINT32 word = RECV_WORD();
    *val1 = (INT16)(word & 0xFFFF);
    *val2 = (INT16)((word >> 16) & 0xFFFF);
    quant_buf_index = 2;
}

static INT16 recv_quant(void)
{
    UINT32 word;

    if (quant_buf_index >= 2)
    {
        word = RECV_WORD();
        quant_buf[0] = (INT16)(word & 0xFFFF);
        quant_buf[1] = (INT16)((word >> 16) & 0xFFFF);
        quant_buf_index = 0;
    }

    return quant_buf[quant_buf_index++];
}

volatile UINT32 g_stage_status = 0;
volatile UINT32 g_stage_count  = 0;

static UINT32 lcode = 0;
static UINT16 bitindex = 0;
static INT16  ldc1 = 0, ldc2 = 0, ldc3 = 0;

#define PUTBITS \
{INT16 _n=(INT16)(bitindex+numbits-32); \
 if(_n<0){lcode=(lcode<<numbits)|data;bitindex+=numbits;} \
 else{UINT8 _b; \
  lcode=(lcode<<(32-bitindex))|(data>>_n); \
  _b=(UINT8)(lcode>>24);SEND_BYTE(_b);if(_b==0xFF)SEND_BYTE(0); \
  _b=(UINT8)(lcode>>16);SEND_BYTE(_b);if(_b==0xFF)SEND_BYTE(0); \
  _b=(UINT8)(lcode>>8); SEND_BYTE(_b);if(_b==0xFF)SEND_BYTE(0); \
  _b=(UINT8) lcode;     SEND_BYTE(_b);if(_b==0xFF)SEND_BYTE(0); \
  lcode=data;bitindex=_n;}}

static void write_markers(UINT32 w, UINT32 h, UINT8 *Lq, UINT8 *Cq)
{
    UINT16 i, hl; UINT8 nc = 3;
    SEND_BYTE(0xFF); SEND_BYTE(0xD8);
    SEND_BYTE(0xFF); SEND_BYTE(0xDB); SEND_BYTE(0x00); SEND_BYTE(0x84);
    SEND_BYTE(0x00); for (i = 0; i < 64; i++) SEND_BYTE(Lq[i]);
    SEND_BYTE(0x01); for (i = 0; i < 64; i++) SEND_BYTE(Cq[i]);
    for (i = 0; i < 210; i++) { SEND_BYTE((UINT8)(markerdata[i] >> 8)); SEND_BYTE((UINT8)markerdata[i]); }
    SEND_BYTE(0xFF); SEND_BYTE(0xC0);
    hl = (UINT16)(8 + 3 * nc); SEND_BYTE((UINT8)(hl >> 8)); SEND_BYTE((UINT8)hl);
    SEND_BYTE(0x08);
    SEND_BYTE((UINT8)(h >> 8)); SEND_BYTE((UINT8)h);
    SEND_BYTE((UINT8)(w >> 8)); SEND_BYTE((UINT8)w);
    SEND_BYTE(nc); SEND_BYTE(0x01); SEND_BYTE(0x11); SEND_BYTE(0x00);
    SEND_BYTE(0x02); SEND_BYTE(0x11); SEND_BYTE(0x01);
    SEND_BYTE(0x03); SEND_BYTE(0x11); SEND_BYTE(0x01);
    SEND_BYTE(0xFF); SEND_BYTE(0xDA);
    hl = (UINT16)(6 + 2 * nc); SEND_BYTE((UINT8)(hl >> 8)); SEND_BYTE((UINT8)hl);
    SEND_BYTE(nc); SEND_BYTE(0x01); SEND_BYTE(0x00);
    SEND_BYTE(0x02); SEND_BYTE(0x11); SEND_BYTE(0x03); SEND_BYTE(0x11);
    SEND_BYTE(0x00); SEND_BYTE(0x3F); SEND_BYTE(0x00);
}

static void huffman(UINT16 comp)
{
    UINT16 i, *DcC, *DcS, *AcC, *AcS, AbsC, HC, HS, RL = 0, DS = 0, idx;
    UINT16 numbits; UINT32 data;
    INT16 Coeff, LastDc;
    Coeff = recv_quant();
    if (comp == 1) { DcC = luminance_dc_code_table; DcS = luminance_dc_size_table; AcC = luminance_ac_code_table; AcS = luminance_ac_size_table; LastDc = ldc1; ldc1 = Coeff; }
    else { DcC = chrominance_dc_code_table; DcS = chrominance_dc_size_table; AcC = chrominance_ac_code_table; AcS = chrominance_ac_size_table; if (comp == 2) { LastDc = ldc2; ldc2 = Coeff; } else { LastDc = ldc3; ldc3 = Coeff; } }
    Coeff -= LastDc;
    AbsC = (Coeff < 0) ? (UINT16)(-Coeff--) : (UINT16)Coeff;
    while (AbsC) { AbsC >>= 1; DS++; }
    HC = DcC[DS]; HS = DcS[DS];
    Coeff &= (INT16)((1 << DS) - 1);
    data = ((UINT32)HC << DS) | (UINT16)Coeff; numbits = HS + DS; PUTBITS
    RL = 0;
    for (i = 63; i > 0; i--)
    {
        Coeff = recv_quant();
        if (Coeff != 0)
        {
            while (RL > 15) { RL -= 16; data = AcC[161]; numbits = AcS[161]; PUTBITS }
            AbsC = (Coeff < 0) ? (UINT16)(-Coeff--) : (UINT16)Coeff;
            DS = (AbsC >> 8 == 0) ? bitsize[AbsC] : bitsize[AbsC >> 8] + 8;
            idx = RL * 10 + DS; HC = AcC[idx]; HS = AcS[idx];
            Coeff &= (INT16)((1 << DS) - 1);
            data = ((UINT32)HC << DS) | (UINT16)Coeff; numbits = HS + DS; PUTBITS
            RL = 0;
        }
        else RL++;
    }
    if (RL) { data = AcC[0]; numbits = AcS[0]; PUTBITS }
}

static void close_bs(void)
{
    UINT16 i, cnt; UINT8 *p, b;
    if (bitindex > 0) { lcode <<= (32 - bitindex); cnt = (bitindex + 7) >> 3; p = (UINT8 *)&lcode + 3; for (i = cnt; i > 0; i--) { b = *p--; SEND_BYTE(b); if (b == 0xFF) SEND_BYTE(0); } }
    SEND_BYTE(0xFF); SEND_BYTE(0xD9);
}

int main(void)
{
    UINT16 i;
    INT16 q0, q1;
    UINT32 image_width, image_height, num_mcus, processed_mcus, total_mcus, percent, last_reported_percent;
    UINT32 ticks_per_sec, stage_start_ticks, stage_elapsed_ticks, stage_elapsed_ms;
    UINT8 Lq[64], Cq[64];

    image_width  = RECV_META();
    image_height = RECV_META();
    num_mcus     = RECV_META();
    total_mcus   = num_mcus;
    processed_mcus = 0;

    g_stage_status = 1;
    g_stage_count = 0;
    printf("started\n");

    for (i = 0; i < 64; i += 2) { recv_quant_pair(&q0, &q1); Lq[i] = (UINT8)q0; Lq[i+1] = (UINT8)q1; }
    for (i = 0; i < 64; i += 2) { recv_quant_pair(&q0, &q1); Cq[i] = (UINT8)q0; Cq[i+1] = (UINT8)q1; }
    recv_quant_pair(&q0, &q1);

    SEND_BYTE((UINT8)(num_mcus >> 24));
    SEND_BYTE((UINT8)(num_mcus >> 16));
    SEND_BYTE((UINT8)(num_mcus >> 8));
    SEND_BYTE((UINT8)(num_mcus));
    g_stage_status = 2;
    printf("received metadata: %ux%u, %u MCUs\n",
           (unsigned)image_width,
           (unsigned)image_height,
           (unsigned)total_mcus);

    lcode = 0; bitindex = 0; ldc1 = 0; ldc2 = 0; ldc3 = 0;
    write_markers(image_width, image_height, Lq, Cq);
    g_stage_status = 3;
    printf("writing Huffman stream\n");
    ticks_per_sec = alt_ticks_per_second();
    if (ticks_per_sec == 0) ticks_per_sec = 1000u;
    stage_start_ticks = alt_nticks();
    last_reported_percent = 0;
    for (; num_mcus > 0; num_mcus--)
    {
        huffman(1); huffman(2); huffman(3);
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
    close_bs();
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
