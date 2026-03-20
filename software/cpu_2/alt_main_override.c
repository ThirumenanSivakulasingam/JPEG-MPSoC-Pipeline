#include <stddef.h>

/*
 * The JPEG pipeline apps use raw MMIO only; skipping the HAL device/stdio
 * startup avoids the shared pre-main path that appears to stall all CPUs.
 */
void alt_main(void)
{
    extern int main(void);

    (void)main();
    for (;;)
        ;
}
