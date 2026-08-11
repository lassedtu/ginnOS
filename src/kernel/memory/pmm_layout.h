#pragma once

#include "../../common/stdint.h"
#include "../../common/boot/boot_info.h"

#define PAGE_SIZE 4096u

/**
 * initialize PMM layout by computing bitmap placement from the E820 map.
 * must be called after kernel_layout is available and before pmm_init().
 * @param boot pointer to boot info containing the E820 memory map.
 */
void pmm_layout_init(boot_info_t *boot);

/**
 * return the start address of the PMM bitmap (page-aligned, after kernel_end).
 * only valid after pmm_layout_init() has been called.
 */
uint32_t pmm_bitmap_start(void);

/**
 * return the end address of the PMM bitmap (exclusive).
 * only valid after pmm_layout_init() has been called.
 */
uint32_t pmm_bitmap_end(void);

/**
 * return the size of the PMM bitmap in bytes.
 * only valid after pmm_layout_init() has been called.
 */
uint32_t pmm_bitmap_size(void);

/**
 * return the total number of page frames the bitmap can track.
 * only valid after pmm_layout_init() has been called.
 */
uint32_t pmm_total_pages(void);
