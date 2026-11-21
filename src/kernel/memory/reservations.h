#pragma once

#include "common/stdint.h"
#include "kernel/memory/phys.h"

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
void memory_reserve_pmm_bitmap(phys_addr_t start, phys_addr_t end);

/**
 * reserve the kernel heap memory region.
 * called by heap_init() once it determines the heap placement.
 * @param start first byte of the heap (inclusive).
 * @param end first byte past the heap (exclusive).
 */
void memory_reserve_heap(phys_addr_t start, phys_addr_t end);
/**
 * reserve a root filesystem image that the bootloader loaded into RAM.
 * nothing is reserved if end <= start.
 * @param start first byte of the image (inclusive).
 * @param end first byte past the image (exclusive).
 */
void memory_reserve_initrd(phys_addr_t start, phys_addr_t end);
