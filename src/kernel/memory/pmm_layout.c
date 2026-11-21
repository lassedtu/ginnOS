#include "pmm_layout.h"

#include "kernel_layout.h"
#include "memory_layout.h"
#include "kernel/panic.h"
#include "kernel/memory/heap.h"

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

/**
 * move a candidate start past a physical range it would overlap.
 */
static phys_addr_t skip_range(phys_addr_t start, phys_addr_t size, phys_addr_t range_start,
                              phys_addr_t range_end)
{
    if (range_end > range_start && start < range_end && start + size > range_start)
    {
        return page_align_up(range_end);
    }
    return start;
}

/**
 * the first address at or above LOW_MEMORY_END in usable RAM where size
 * bytes fit without the kernel image or the boot module, or 0.
 */
static phys_addr_t find_metadata_area(const boot_info_t *boot, phys_addr_t size)
{
    phys_addr_t kernel_start = virt_to_phys((void *)kernel_start_address());
    phys_addr_t kernel_end = page_align_up(virt_to_phys((void *)kernel_end_address()));

    for (uint32_t i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        const memory_region_t *region = &boot->memory_map.regions[i];
        if (region->type != 1 || region->base >= PHYS_MAP_SIZE)
        {
            continue;
        }

        uint64_t region_end = region->base + region->length;
        if (region_end > PHYS_MAP_SIZE)
        {
            region_end = PHYS_MAP_SIZE;
        }

        phys_addr_t start = page_align_up((phys_addr_t)region->base);
        if (start < LOW_MEMORY_END)
        {
            start = LOW_MEMORY_END;
        }

        // two passes: moving past one range can move into the other
        for (int pass = 0; pass < 2; pass++)
        {
            start = skip_range(start, size, kernel_start, kernel_end);
            start = skip_range(start, size, boot->initrd_start, boot->initrd_end);
        }

        if ((uint64_t)start + size <= region_end)
        {
            return start;
        }
    }
    return 0;
}

void pmm_layout_init(boot_info_t *boot)
{
    uint64_t highest_address = 0;
    uint32_t i;

    /* walk the memory map to find the highest usable byte address */
    for (i = 0; i < boot->memory_map.count && i < 32u; i++)
    {
        memory_region_t *region = &boot->memory_map.regions[i];

        /* type 1 = usable RAM */
        if (region->type != 1)
        {
            continue;
        }

        uint64_t region_end = region->base + region->length;
        if (region_end > highest_address)
        {
            highest_address = region_end;
        }
    }

    if (highest_address == 0)
    {
        kernel_panic("pmm_layout_init: no usable memory found");
    }

    /* the kernel reaches RAM through the direct map, which covers at most
     * PHYS_MAP_SIZE bytes: RAM above that is left unused */
    if (highest_address > PHYS_MAP_SIZE)
    {
        highest_address = PHYS_MAP_SIZE;
    }

    /* total page frames the bitmap must track */
    total_pages = (uint32_t)(highest_address / PAGE_SIZE);

    /* bitmap size: 1 bit per page, rounded up to whole bytes */
    bitmap_bytes = (total_pages + 7) / 8;

    /* the bitmap, then the reference counts (one uint16_t per page), then
     * the first heap pages (heap_init), in one piece of usable RAM above
     * the legacy area, away from the kernel image and the boot module. */
    phys_addr_t refcount_offset = (bitmap_bytes + 3u) & ~(phys_addr_t)3u;
    phys_addr_t metadata_size =
        page_align_up(refcount_offset + (phys_addr_t)total_pages * sizeof(uint16_t));
    phys_addr_t need = metadata_size + HEAP_INITIAL_PAGES * PAGE_SIZE;

    bitmap_start = find_metadata_area(boot, need);
    if (bitmap_start == 0)
    {
        kernel_panic("pmm_layout_init: no room for the PMM metadata");
    }

    refcount_start = bitmap_start + refcount_offset;
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
