/*
 * cpu_0 / main.c  — with debug prints
 * Safe variant: input parameter block moved away from cpu_0 SDRAM text.
 */
#include <stdio.h>
#include <io.h>
#include "sys/alt_alarm.h"
#include "jpeg_addrs.h"
#include "datatype.h"
#include "jdatatype.h"
#include "config.h"
#include "avalon_fifo.h"

#define IMAGE_PARAM_BASE  ((UINT32)0x02400000)
#define IMAGE_DATA_BASE   (IMAGE_PARAM_BASE + 0x100)
#define CPU0_STATUS_ADDR  ((UINT32)0x02600020)
#define CPU0_MCU_ADDR     ((UINT32)0x02600024)

#define SEND_PIX_PAIR(val1, val2) \
    FIFO_WRITE_BLOCK(FIFO_0_1_1_IN_BASE, FIFO_0_1_1_IN_CSR_BASE, ((UINT32)(val1) & 0xFFFF) | (((UINT32)(val2) & 0xFFFF) << 16))
#define SEND_META(val)  FIFO_WRITE_BLOCK(FIFO_0_1_5_IN_BASE, FIFO_0_1_5_IN_CSR_BASE, (UINT32)(val))

static void initialization(JPEG_ENCODER_STRUCTURE *jpeg,
                            UINT32 image_width, UINT32 image_height)
{
    UINT16 mcu_width = 8, mcu_height = 8, bytes_per_pixel = 3;
    jpeg->mcu_width   = mcu_width;
    jpeg->mcu_height  = mcu_height;
    jpeg->horizontal_mcus = (UINT16)((image_width  + mcu_width  - 1) >> 3);
    jpeg->vertical_mcus   = (UINT16)((image_height + mcu_height - 1) >> 3);
    jpeg->rows_in_bottom_mcus = (UINT16)(image_height - (jpeg->vertical_mcus  - 1) * mcu_height);
    jpeg->cols_in_right_mcus  = (UINT16)(image_width  - (jpeg->horizontal_mcus - 1) * mcu_width);
    jpeg->length_minus_mcu_width = (UINT16)((image_width - mcu_width) * bytes_per_pixel);
    jpeg->length_minus_width     = (UINT16)((image_width - jpeg->cols_in_right_mcus) * bytes_per_pixel);
    jpeg->mcu_width_size         = (UINT16)(mcu_width * bytes_per_pixel);
    jpeg->offset = (UINT16)((image_width * (mcu_height - 1)
                   - (mcu_width - jpeg->cols_in_right_mcus)) * bytes_per_pixel);
}

static void rgb_to_ycbcr(UINT8 R, UINT8 G, UINT8 B,
                         UINT8 *Y_out, UINT8 *Cb_out, UINT8 *Cr_out)
{
    INT32 Y, Cb, Cr;

    Y  = ((77 * R + 150 * G +  29 * B) >> 8);
    Cb = ((-43 * R - 85 * G + 128 * B) >> 8) + 128;
    Cr = ((128 * R - 107 * G - 21 * B) >> 8) + 128;

    if (Y < 0) Y = 0; else if (Y > 255) Y = 255;
    if (Cb < 0) Cb = 0; else if (Cb > 255) Cb = 255;
    if (Cr < 0) Cr = 0; else if (Cr > 255) Cr = 255;

    *Y_out = (UINT8)Y;
    *Cb_out = (UINT8)Cb;
    *Cr_out = (UINT8)Cr;
}

static void read_444_format(JPEG_ENCODER_STRUCTURE *jpeg, UINT8 *input_ptr)
{
    INT32 i, j;
    UINT8 R, G, B;
    UINT8 Y, Cb, Cr;
    UINT8 Yblk[64], Cbblk[64], Crblk[64];
    UINT16 idx = 0;
    UINT16 rows = jpeg->rows, cols = jpeg->cols, incr = jpeg->incr;

    for (i = rows; i > 0; i--)
    {
        for (j = cols; j > 0; j--)
        {
            R = *input_ptr++; G = *input_ptr++; B = *input_ptr++;
            rgb_to_ycbcr(R, G, B, &Y, &Cb, &Cr);
            Yblk[idx] = Y;
            Cbblk[idx] = Cb;
            Crblk[idx] = Cr;
            idx++;
        }
        if ((j = (8 - cols)) > 0)
        {
            R = *(input_ptr - 3); G = *(input_ptr - 2); B = *(input_ptr - 1);
            rgb_to_ycbcr(R, G, B, &Y, &Cb, &Cr);
            for (; j > 0; j--)
            {
                Yblk[idx] = Y;
                Cbblk[idx] = Cb;
                Crblk[idx] = Cr;
                idx++;
            }
        }
        input_ptr += incr;
    }
    input_ptr -= incr;
    for (i = 8 - rows; i > 0; i--)
    {
        for (j = cols * 3; j > 0;)
        {
            R = *(input_ptr - (j--));
            G = *(input_ptr - (j--));
            B = *(input_ptr - (j--));
            rgb_to_ycbcr(R, G, B, &Y, &Cb, &Cr);
            Yblk[idx] = Y;
            Cbblk[idx] = Cb;
            Crblk[idx] = Cr;
            idx++;
        }
        if ((j = (8 - cols)) > 0)
        {
            R = *(input_ptr - 3); G = *(input_ptr - 2); B = *(input_ptr - 1);
            rgb_to_ycbcr(R, G, B, &Y, &Cb, &Cr);
            for (; j > 0; j--)
            {
                Yblk[idx] = Y;
                Cbblk[idx] = Cb;
                Crblk[idx] = Cr;
                idx++;
            }
        }
    }

    for (idx = 0; idx < 64; idx += 2) SEND_PIX_PAIR(Yblk[idx], Yblk[idx+1]);
    for (idx = 0; idx < 64; idx += 2) SEND_PIX_PAIR(Cbblk[idx], Cbblk[idx+1]);
    for (idx = 0; idx < 64; idx += 2) SEND_PIX_PAIR(Crblk[idx], Crblk[idx+1]);
}

int main(void)
{
    UINT32 quality_factor, image_format, image_width, image_height, num_mcus;
    UINT32 percent, last_reported_percent;
    UINT32 ticks_per_sec, stream_start_ticks, stream_elapsed_ticks, stream_elapsed_ms;
    UINT16 i, j;
    JPEG_ENCODER_STRUCTURE JpegStruct;
    JPEG_ENCODER_STRUCTURE *jpeg = &JpegStruct;
    UINT8 *input_ptr;
    UINT32 mcu_index = 0;

    IOWR_32DIRECT(CPU0_MCU_ADDR, 0, 0);
    IOWR_32DIRECT(CPU0_STATUS_ADDR, 0, 1);
    printf("reading image header from SDRAM\n");
    quality_factor = IORD_32DIRECT(IMAGE_PARAM_BASE,  0);
    image_format   = IORD_32DIRECT(IMAGE_PARAM_BASE,  4);
    image_width    = IORD_32DIRECT(IMAGE_PARAM_BASE,  8);
    image_height   = IORD_32DIRECT(IMAGE_PARAM_BASE, 12);

    if (image_format == RGB) image_format = FOUR_FOUR_FOUR;
    input_ptr = (UINT8 *)IMAGE_DATA_BASE;
    initialization(jpeg, image_width, image_height);
    num_mcus = (UINT32)(jpeg->vertical_mcus) * (UINT32)(jpeg->horizontal_mcus);
    SEND_PIX_PAIR(num_mcus & 0xFFFF, (num_mcus >> 16) & 0xFFFF);
    SEND_PIX_PAIR(quality_factor & 0xFFFF, (quality_factor >> 16) & 0xFFFF);

    IOWR_32DIRECT(CPU0_STATUS_ADDR, 0, 2);
    printf("metadata sent: %u MCUs, q=%u, %ux%u\n",
           (unsigned)num_mcus,
           (unsigned)quality_factor,
           (unsigned)image_width,
           (unsigned)image_height);
    SEND_META(image_width);
    SEND_META(image_height);
    SEND_META(num_mcus);

    IOWR_32DIRECT(CPU0_STATUS_ADDR, 0, 3);
    printf("streaming MCU blocks to cpu1\n");
    ticks_per_sec = alt_ticks_per_second();
    if (ticks_per_sec == 0) ticks_per_sec = 1000u;
    stream_start_ticks = alt_nticks();
    last_reported_percent = 0;
    for (i = 1; i <= jpeg->vertical_mcus; i++)
    {
        jpeg->rows = (i < jpeg->vertical_mcus) ?
                     jpeg->mcu_height : jpeg->rows_in_bottom_mcus;
        for (j = 1; j <= jpeg->horizontal_mcus; j++)
        {
            if (j < jpeg->horizontal_mcus)
            { jpeg->cols = jpeg->mcu_width; jpeg->incr = jpeg->length_minus_mcu_width; }
            else
            { jpeg->cols = jpeg->cols_in_right_mcus; jpeg->incr = jpeg->length_minus_width; }
            read_444_format(jpeg, input_ptr);
            mcu_index++;
            IOWR_32DIRECT(CPU0_MCU_ADDR, 0, mcu_index);
            if (num_mcus > 0)
            {
                percent = (mcu_index * 100u) / num_mcus;
                percent = (percent / 10u) * 10u;
                if (percent >= last_reported_percent + 10u)
                {
                    printf("%u%% (%u/%u MCUs)\n",
                           (unsigned)percent,
                           (unsigned)mcu_index,
                           (unsigned)num_mcus);
                    last_reported_percent = percent;
                }
            }
            input_ptr += jpeg->mcu_width_size;
        }
        input_ptr += jpeg->offset;
    }

    IOWR_32DIRECT(CPU0_STATUS_ADDR, 0, 4);
    printf("finished streaming all %u MCUs\n", (unsigned)mcu_index);
    stream_elapsed_ticks = alt_nticks() - stream_start_ticks;
    stream_elapsed_ms = (stream_elapsed_ticks * 1000u) / ticks_per_sec;
    if (stream_elapsed_ms == 0) stream_elapsed_ms = 1u;
    printf("METRICS elapsed_ms=%u mcus=%u\n",
           (unsigned)stream_elapsed_ms,
           (unsigned)mcu_index);
    for (;;)
        ;
}
