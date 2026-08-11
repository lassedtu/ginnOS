#include "region.h"

#include "../panic.h"
#include "../../common/stdio.h"

static region_entry_t region_table[REGION_TABLE_MAX];
static uint32_t region_table_count = 0;

void region_reserve(uint32_t start, uint32_t end, const char *label)
{
    if (start >= end)
    {
        kernel_panic("region_reserve: start >= end");
    }

    if (region_table_count >= REGION_TABLE_MAX)
    {
        kernel_panic("region_reserve: table full");
    }

    region_table[region_table_count].start = start;
    region_table[region_table_count].end = end;
    region_table[region_table_count].label = label;
    region_table_count++;
}

bool region_is_reserved(uint32_t address)
{
    uint32_t i;

    for (i = 0; i < region_table_count; i++)
    {
        if (address >= region_table[i].start && address < region_table[i].end)
        {
            return true;
        }
    }

    return false;
}

bool region_overlaps(uint32_t start, uint32_t end)
{
    uint32_t i;

    for (i = 0; i < region_table_count; i++)
    {
        if (start < region_table[i].end && end > region_table[i].start)
        {
            return true;
        }
    }

    return false;
}

uint32_t region_count(void)
{
    return region_table_count;
}

void region_print_all(void)
{
    uint32_t i;

    printf("Reserved memory regions (%u):\r\n", region_table_count);

    for (i = 0; i < region_table_count; i++)
    {
        printf("  [%u] 0x%x - 0x%x (%u bytes) %s\r\n",
               i,
               region_table[i].start,
               region_table[i].end,
               region_table[i].end - region_table[i].start,
               region_table[i].label ? region_table[i].label : "unknown");
    }
}
