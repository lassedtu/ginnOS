#include "common/stdint.h"
#include "common/stdio.h"
#include "common/boot/boot_info.h"

void memory_print_map(boot_info_t *boot)
{
    uint32_t i;
    for (i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        memory_region_t *region = &boot->memory_map.regions[i];
        printf(
            "[%u] base=0x%x%x len=0x%x%x type=%u\r\n",
            i,
            (uint32_t)(region->base >> 32),
            (uint32_t)region->base,
            (uint32_t)(region->length >> 32),
            (uint32_t)region->length,
            region->type);
    }
}