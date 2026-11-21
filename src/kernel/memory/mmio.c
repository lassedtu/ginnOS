#include "mmio.h"

#include "memory_layout.h"
#include "page.h"
#include "arch/arch.h"
#include "kernel/sync/spinlock.h"

// next unused address in the MMIO window (guarded by mmio_lock)
static uintptr_t mmio_next;
static spinlock_t mmio_lock;

void mmio_init(void)
{
    spinlock_init(&mmio_lock);
    mmio_next = MMIO_REGION_BASE;
    arch_reserve_kernel_range(MMIO_REGION_BASE, MMIO_REGION_END);
}

/**
 * map [phys, phys + size) into the MMIO window, cached or not.
 */
static void *map_range(phys_addr_t phys, size_t size, bool cached)
{
    phys_addr_t first = PAGE_ALIGN_DOWN(phys);
    size_t bytes = PAGE_ALIGN_UP(phys + size) - first;

    // claim a virtual range from the window
    uint32_t flags = spin_lock_irqsave(&mmio_lock);
    uintptr_t virt = mmio_next;
    bool fits = bytes <= MMIO_REGION_END - virt;
    if (fits)
    {
        mmio_next += bytes;
    }
    spin_unlock_irqrestore(&mmio_lock, flags);

    if (!fits)
    {
        return NULL;
    }

    for (size_t offset = 0; offset < bytes; offset += PAGE_SIZE)
    {
        if (cached)
        {
            arch_map_kernel_page(virt + offset, first + offset);
        }
        else
        {
            arch_map_device_page(virt + offset, first + offset);
        }
    }

    return (void *)(virt + (phys - first));
}

void *ioremap(phys_addr_t phys, size_t size)
{
    return map_range(phys, size, true);
}

void *ioremap_nocache(phys_addr_t phys, size_t size)
{
    return map_range(phys, size, false);
}
