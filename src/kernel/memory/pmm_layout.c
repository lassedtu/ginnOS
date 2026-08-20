#include "pmm_layout.h"

#include "kernel_layout.h"
#include "kernel/panic.h"

static uint32_t bitmap_start;
static uint32_t bitmap_end;
static uint32_t bitmap_bytes;
static uint32_t total_pages;

/**
 * round an address up to the next page boundary.
 */
static uint32_t page_align_up(uint32_t addr)
{
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void pmm_layout_init(boot_info_t *boot)
{
    uint32_t highest_address = 0;
    uint32_t i;

    /* walk the E820 map to find the highest usable byte address */
    for (i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        memory_region_t *region = &boot->memory_map.regions[i];

        /* type 1 = usable RAM */
        if (region->type != 1)
        {
            continue;
        }

        /* ignore regions above 4 GiB (we are 32-bit) */
        if (region->base >> 32)
        {
            continue;
        }

        uint64_t region_end = region->base + region->length;

        /* cap at 4 GiB */
        if (region_end > 0xFFFFFFFFULL)
        {
            region_end = 0xFFFFFFFFULL;
        }

        if ((uint32_t)region_end > highest_address)
        {
            highest_address = (uint32_t)region_end;
        }
    }

    if (highest_address == 0)
    {
        kernel_panic("pmm_layout_init: no usable memory found");
    }

    /* total page frames the bitmap must track */
    total_pages = highest_address / PAGE_SIZE;

    /* bitmap size: 1 bit per page, rounded up to whole bytes */
    bitmap_bytes = (total_pages + 7) / 8;

    /* place bitmap right after kernel_end, page-aligned */
    bitmap_start = page_align_up(kernel_end_address());
    bitmap_end = bitmap_start + bitmap_bytes;
}

uint32_t pmm_bitmap_start(void)
{
    return bitmap_start;
}

uint32_t pmm_bitmap_end(void)
{
    return bitmap_end;
}

uint32_t pmm_bitmap_size(void)
{
    return bitmap_bytes;
}

uint32_t pmm_total_pages(void)
{
    return total_pages;
}
