#pragma once

#include "../../common/stdint.h"

/**
 * reserve the stage2 bootloader memory region.
 */
void memory_reserve_stage2(void);

/**
 * reserve the kernel memory region (kernel_start to kernel_end).
 */
void memory_reserve_kernel(void);

/**
 * reserve the PMM bitmap memory region.
 * called by the PMM once it determines where to place its bitmap.
 * @param start first byte of the bitmap (inclusive).
 * @param end first byte past the bitmap (exclusive).
 */
void memory_reserve_pmm_bitmap(uint32_t start, uint32_t end);