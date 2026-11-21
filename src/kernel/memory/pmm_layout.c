#include "pmm_layout.h"

#include "kernel_layout.h"
#include "memory_layout.h"
#include "kernel/panic.h"

static phys_addr_t bitmap_start;
static phys_addr_t bitmap_end;
static size_t bitmap_bytes;
static phys_addr_t refcount_start;
static uint32_t total_pages;

/**
 * round an address up to the next page boundary.
 */
static uintptr_t page_align_up(uintptr_t addr)
{
    return PAGE_ALIGN_UP(addr);
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

    /* the kernel reaches RAM through the direct map, which covers at most
     * PHYS_MAP_SIZE bytes on 32-bit: RAM above that is left unused */
    if (highest_address > PHYS_MAP_SIZE)
    {
        highest_address = PHYS_MAP_SIZE;
    }

    /* total page frames the bitmap must track */
    total_pages = highest_address / PAGE_SIZE;

    /* bitmap size: 1 bit per page, rounded up to whole bytes */
    bitmap_bytes = (total_pages + 7) / 8;

    /* place the bitmap after the kernel and above the legacy area. the heap
     * comes next (heap_init), so neither can reach VGA memory. the usable
     * pages between the kernel and LOW_MEMORY_END go to the PMM. */
    bitmap_start = page_align_up(virt_to_phys((void *)kernel_end_address()));
    if (bitmap_start < LOW_MEMORY_END)
    {
        bitmap_start = LOW_MEMORY_END;
    }

    /* a multiboot 2 bootloader puts a module (the root filesystem image)
     * after the kernel. the bitmap and the heap must not overwrite it. */
    if (boot->initrd_end != 0 && boot->initrd_start < PHYS_MAP_SIZE &&
        bitmap_start < boot->initrd_end)
    {
        bitmap_start = page_align_up(boot->initrd_end);
    }

    /* the reference counts (one uint16_t per page) follow the bitmap. the
     * reserved range covers both. */
    refcount_start = (bitmap_start + bitmap_bytes + 3u) & ~(phys_addr_t)3u;
    bitmap_end = refcount_start + (phys_addr_t)total_pages * sizeof(uint16_t);
}

phys_addr_t pmm_bitmap_start(void)
{
    return bitmap_start;
}

phys_addr_t pmm_bitmap_end(void)
{
    return bitmap_end;
}

phys_addr_t pmm_refcount_start(void)
{
    return refcount_start;
}

size_t pmm_bitmap_size(void)
{
    return bitmap_bytes;
}

uint32_t pmm_total_pages(void)
{
    return total_pages;
}
