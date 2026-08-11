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
    region_reserve(kernel_start_address(), kernel_end_address(), "kernel");
}

void memory_reserve_pmm_bitmap(uint32_t start, uint32_t end)
{
    region_reserve(start, end, "pmm_bitmap");
}

void memory_reserve_heap(uint32_t start, uint32_t end)
{
    region_reserve(start, end, "kernel_heap");
}
