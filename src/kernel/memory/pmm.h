#pragma once

#include "../../common/stdint.h"
#include "../../common/boot/boot_info.h"

/**
 * initialize the physical memory manager.
 * must be called after pmm_layout_init() and all memory reservations.
 * walks the E820 map and marks usable, non-reserved pages as free.
 * @param boot pointer to boot info containing the E820 memory map.
 */
void pmm_init(boot_info_t *boot);

/**
 * allocate a single 4 KiB physical page frame.
 * @return physical address of the allocated page, or 0 on failure.
 */
void *pmm_alloc_page(void);

/**
 * free a previously allocated physical page frame.
 * @param address physical address of the page to free (must be page-aligned).
 */
void pmm_free_page(void *address);

/**
 * return the number of free page frames available for allocation.
 * @returns the number of free page frames.
 */
uint32_t pmm_free_count(void);

/**
 * return the total number of page frames managed by the PMM.
 * @returns the total number of page frames.
 */
uint32_t pmm_total_count(void);
