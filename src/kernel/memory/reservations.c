#include "reservations.h"

#include "region.h"
#include "boot_layout.h"
#include "kernel_layout.h"

void memory_reserve_stage2(void)
{
    region_reserve(stage2_reserved_start(), stage2_reserved_end(), "stage2");
}

void memory_reserve_kernel(void)
{
    region_reserve(virt_to_phys((void *)kernel_start_address()),
                   virt_to_phys((void *)kernel_end_address()), "kernel");
}

void memory_reserve_pmm_bitmap(phys_addr_t start, phys_addr_t end)
{
    region_reserve(start, end, "pmm_bitmap");
}

void memory_reserve_heap(phys_addr_t start, phys_addr_t end)
{
    region_reserve(start, end, "kernel_heap");
}
