# JPEG Encoder MPSoC

This repository contains a six-stage JPEG encoder pipeline built on a multi-Nios II MPSoC design. The image is processed stage by stage through hardware FIFOs, and the final JPEG is reconstructed on the host from the CPU5 UART dump.

This version of the project is cleaned for sharing. It keeps the source files, one sample input image, one sample output image, and the scripts needed to rebuild and run the project.

## Pipeline Overview

- `cpu0`: read image data, form MCUs, convert RGB to YCbCr
- `cpu1`: level shift samples by subtracting 128
- `cpu2`: perform 8x8 DCT, with optional custom-instruction acceleration
- `cpu3`: quantize coefficients and apply zigzag reordering
- `cpu4`: generate JPEG markers and Huffman-encode the stream
- `cpu5`: collect the final JPEG byte stream and dump it over JTAG UART

## Important Files

- `JPEG_SoC.qsys`: Platform Designer system
- `jpeg_dct_ci.v`: custom DCT instruction RTL used by the project
- `jpeg_dct_hw.tcl`: custom instruction packaging file
- `software/run_jpeg.sh`: full build, download, run, monitor, and extract script
- `software/build_image_srec.py`: builds the SDRAM image from raw input
- `software/convert_image_to_raw.py`: converts normal images to raw RGB input
- `software/extract_jpeg_uart.py`: reconstructs the JPEG from the CPU5 UART dump

## What Is Included

- One sample input image:
  - `input.jpg`
- One sample output image:
  - `software/generated/output.jpg`

## Requirements

- Quartus / Platform Designer compatible with this project
- Nios II EDS tools on `PATH`
- Python 3
- Python Pillow package for image conversion
- Board and USB-Blaster connection

## Hardware Flow

If you want to rebuild the hardware:

1. Open `JPEG_SoC.qsys` in Platform Designer.
2. Make sure the custom DCT component points to the local project files:
   - `jpeg_dct_ci.v`
   - `jpeg_dct_hw.tcl`
3. Generate the Platform Designer system.
4. Recompile the Quartus project.
5. Regenerate the BSPs if the hardware system changed.

The runner script expects the current hardware outputs to exist when you execute the full flow.

## Software Flow

From the `software` folder, run:

```bash
IMAGE_INPUT=../input.jpg bash run_jpeg.sh
```

This script will:

- rebuild all CPU applications
- convert the input image to raw if needed
- build the SDRAM SREC image
- program the FPGA
- download all CPU ELFs
- monitor live progress through JTAG UART
- print per-stage throughput
- reconstruct the final JPEG as `software/generated/output.jpg`

## Using the Custom DCT Instruction

To test the custom DCT path:

```bash
IMAGE_INPUT=../input.jpg CPU2_USE_CUSTOM_DCT=1 bash run_jpeg.sh
```

In the CPU2 log, look for:

- `custom DCT self-test PASS`
- `using custom DCT accelerator`

If the self-test fails, CPU2 falls back to the software DCT path automatically so that the image output stays correct.

## Throughput Reporting

At the end of a run, `run_jpeg.sh` prints:

- CPU status summary
- MCU counts processed by each stage
- JPEG size
- throughput of `cpu0` to `cpu4` in MCUs/s
- throughput of `cpu5` in bytes/s and MCUs/s

This makes it easy to compare before and after optimizations and identify the true bottleneck stage.

## Notes

- The main bottlenecks in the final working system are the early stages, especially `cpu0` and `cpu2`.
- FIFO enlargement alone did not produce a large end-to-end speedup.
- The biggest throughput gain came from the final software and accelerator-related improvements while preserving correct JPEG output.

