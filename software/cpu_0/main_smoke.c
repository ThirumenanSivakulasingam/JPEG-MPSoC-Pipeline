#include <io.h>

#define CPU0_STATUS_ADDR       0x02600020u
#define CPU0_MCU_ADDR          0x02600024u

int main(void)
{
    IOWR_32DIRECT(CPU0_STATUS_ADDR, 0, 0x12345678u);
    IOWR_32DIRECT(CPU0_MCU_ADDR, 0, 0xCAFEBABEu);

    for (;;)
        ;
}
