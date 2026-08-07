#include "reservations.h"

#include "boot_layout.h"

#include "../../common/stdio.h"

void memory_reserve_stage2(void)
{
    printf("Memory reserve: stage2 [0x%x, 0x%x) (%u bytes)\r\n",
           stage2_reserved_start(),
           stage2_reserved_end(),
           stage2_reserved_size());
}