#include "ksyms.h"

#include "common/stddef.h"

// bounds of .text, from linker/kernel.ld.
extern const uint8_t __text_start[];
extern const uint8_t __text_end[];

bool ksym_is_kernel_text(uint32_t addr)
{
    return addr >= (uint32_t)__text_start && addr < (uint32_t)__text_end;
}

const char *ksym_lookup(uint32_t addr, uint32_t *offset)
{
    if (!ksym_is_kernel_text(addr) || ksym_count == 0 || addr < ksym_table[0].addr)
    {
        return NULL;
    }

    // binary search for the last symbol starting at or below addr.
    uint32_t low = 0;
    uint32_t high = ksym_count;
    while (high - low > 1)
    {
        uint32_t mid = low + (high - low) / 2;
        if (ksym_table[mid].addr <= addr)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    if (offset)
    {
        *offset = addr - ksym_table[low].addr;
    }
    return &ksym_names[ksym_table[low].name_offset];
}
