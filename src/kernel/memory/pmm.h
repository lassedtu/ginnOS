#pragma once

/**
 * physical memory manager: a bitmap of 4 KiB page frames.
 *
 * every function is safe to call from preemptible kernel code: the bitmap
 * and counters are guarded by a spinlock that masks interrupts, so a
 * process switch can never land in the middle of an update.
 */

#include "common/stdint.h"
#include "common/boot/boot_info.h"
#include "kernel/memory/phys.h"

/**
 * initialize the physical memory manager.
 * must be called after pmm_layout_init() and all memory reservations.
 * walks the E820 map and marks usable, non-reserved pages as free.
 * @param boot pointer to boot info containing the E820 memory map.
 */
void pmm_init(boot_info_t *boot);

/**
 * allocate a single 4 KiB physical page frame.
 * @return direct-map address of the page (one reference), or NULL on
 *         failure.
 */
void *pmm_alloc_page(void);

/**
 * allocate a run of physically contiguous 4 KiB page frames.
 * pages are freed individually with pmm_free_page().
 * @param count number of pages (must be > 0).
 * @return physical address of the first page, or NULL if no free run of
 *         that length exists.
 */
void *pmm_alloc_contiguous(uint32_t count);

/**
 * drop one reference to a page frame. the frame becomes free when its last
 * reference goes. a frame from an allocation has one reference.
 * @param address direct-map address of the page (must be page-aligned).
 */
void pmm_free_page(void *address);

/**
 * add a reference to an allocated page frame, so that it stays allocated
 * until pmm_free_page has been called once more. used to share a frame
 * between address spaces (copy-on-write).
 * @param address direct-map address of the page (must be page-aligned).
 */
void pmm_page_ref(void *address);

/**
 * read the reference count of a page frame.
 * @param address direct-map address of the page (must be page-aligned).
 * @return the number of references, 0 for a free or reserved frame.
 */
uint32_t pmm_page_refcount(void *address);

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

/**
 * check whether a physical address falls within the RAM the PMM manages.
 * device memory (e.g. a linear framebuffer mapped high) is not owned by the
 * PMM, so it must not be handed to pmm_free_page. address-space teardown uses
 * this to skip freeing such mappings.
 * @param phys the physical address to test.
 * @return true if the address is a PMM-managed frame, false otherwise.
 */
bool pmm_owns(phys_addr_t phys);

/**
 * mark all page frames within a physical address range as used.
 * used by subsystems that claim memory after pmm_init() (e.g., kernel heap).
 * @param start first byte of the region (inclusive, page-aligned).
 * @param end first byte past the region (exclusive, page-aligned).
 */
void pmm_mark_region_used(phys_addr_t start, phys_addr_t end);

/**
 * check whether a specific page frame is free. the answer can be stale as
 * soon as it returns; to take pages, use pmm_claim_pages().
 * @param page_index index of the page frame to check.
 * @return true if the page is free, false if used or out of range.
 */
bool pmm_is_page_free(uint32_t page_index);

/**
 * claim a run of specific page frames, only if every one of them is free.
 * the check and the claim are one atomic step.
 * @param first_page index of the first page frame.
 * @param count number of page frames.
 * @return true if all pages were free and are now used; false (nothing
 *         claimed) otherwise.
 */
bool pmm_claim_pages(uint32_t first_page, uint32_t count);
