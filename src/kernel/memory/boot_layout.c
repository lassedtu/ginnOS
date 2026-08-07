#include "boot_layout.h"

#define BOOT_RESERVED_START 0x7C00u
#define BOOT_RESERVED_END 0xFC00u

uint32_t stage2_reserved_start(void)
{
    return BOOT_RESERVED_START;
}

uint32_t stage2_reserved_end(void)
{
    return BOOT_RESERVED_END;
}

uint32_t stage2_reserved_size(void)
{
    return stage2_reserved_end() - stage2_reserved_start();
}