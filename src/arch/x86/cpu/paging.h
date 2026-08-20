#pragma once

#include "common/stdint.h"

// Page size
#define PAGE_SIZE 4096u

// Page Directory Entry (PDE) flags
#define PDE_PRESENT 0x001u       // page table is present in memory
#define PDE_READ_WRITE 0x002u    // page table is writable
#define PDE_USER 0x004u          // page table is accessible from ring 3
#define PDE_WRITE_THROUGH 0x008u // write-through caching policy
#define PDE_CACHE_DISABLE 0x010u // disable caching for this page table
#define PDE_ACCESSED 0x020u      // set by CPU when any page in the table is accessed
#define PDE_PAGE_SIZE 0x080u     // 4 MiB pages (not used; we use 4 KiB)

// Page Table Entry (PTE) flags
#define PTE_PRESENT 0x001u       // page is present in memory
#define PTE_READ_WRITE 0x002u    // page is writable
#define PTE_USER 0x004u          // page is accessible from ring 3
#define PTE_WRITE_THROUGH 0x008u // write-through caching policy
#define PTE_CACHE_DISABLE 0x010u // disable caching for this page
#define PTE_ACCESSED 0x020u      // set by CPU when the page is accessed
#define PTE_DIRTY 0x040u         // set by CPU when the page is written to
#define PTE_GLOBAL 0x100u        // page is not flushed from TLB on CR3 reload

// extract the page directory index (bits 22–31) from a virtual address.
#define PAGE_DIR_INDEX(vaddr) (((uint32_t)(vaddr) >> 22) & 0x3FF)

// extract the page table index (bits 12–21) from a virtual address.
#define PAGE_TABLE_INDEX(vaddr) (((uint32_t)(vaddr) >> 12) & 0x3FF)

// extract the physical frame address from a PDE or PTE (upper 20 bits).
#define PAGE_FRAME(entry) ((uint32_t)(entry) & 0xFFFFF000u)

// number of entries in a page directory or page table.
#define PAGE_ENTRIES 1024u

// kernel read/write page: present, writable, supervisor-only.
#define PTE_KERNEL_RW (PTE_PRESENT | PTE_READ_WRITE)

// user read/write page: present, writable, user-accessible.
#define PTE_USER_RW (PTE_PRESENT | PTE_READ_WRITE | PTE_USER)

// kernel read/write page directory entry.
#define PDE_KERNEL_RW (PDE_PRESENT | PDE_READ_WRITE)

// user read/write page directory entry.
#define PDE_USER_RW (PDE_PRESENT | PDE_READ_WRITE | PDE_USER)

/**
 * a page directory: array of 1024 32-bit entries.
 * each entry points to a page table (or is not present).
 */
typedef uint32_t page_directory_t[PAGE_ENTRIES];

/**
 * a page table: array of 1024 32-bit entries.
 * each entry maps a 4 KiB virtual page to a physical frame.
 */
typedef uint32_t page_table_t[PAGE_ENTRIES];

/**
 * initialize the paging subsystem.
 * allocates the kernel page directory, identity-maps all usable physical RAM,
 * installs the page fault handler, loads CR3, and enables paging.
 * must be called after heap_init() and before io_enable_interrupts().
 */
void paging_init(void);

/**
 * map a single 4 KiB virtual page to a physical frame.
 * allocates a new page table from the PMM if the relevant PDE is not present.
 * @param virt virtual address (must be page-aligned).
 * @param phys physical address (must be page-aligned).
 * @param flags PTE flags to apply (e.g. PTE_KERNEL_RW).
 * @return true on success, false if page table allocation fails.
 */
bool paging_map(uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * unmap a single 4 KiB virtual page.
 * clears the PTE and invalidates the TLB entry.
 * @param virt virtual address to unmap (must be page-aligned).
 */
void paging_unmap(uint32_t virt);

/**
 * translate a virtual address to its mapped physical address.
 * walks the current kernel page directory.
 * @param virt virtual address to translate.
 * @return physical address, or 0 if the page is not mapped.
 */
uint32_t paging_get_physical(uint32_t virt);

/**
 * return the physical address of the kernel page directory.
 * only valid after paging_init() has been called.
 */
uint32_t paging_directory_address(void);

/**
 * return the number of page tables currently allocated.
 * only valid after paging_init() has been called.
 */
uint32_t paging_table_count(void);

/**
 * check whether paging is currently enabled (CR0.PG bit).
 * @return true if paging is enabled, false otherwise.
 */
bool paging_is_enabled(void);

/**
 * clone the kernel page directory for a new process.
 * copies kernel entries (indices 768–1023) from the kernel page directory.
 * user entries (indices 0–767) are left empty (not present).
 * @return physical address of the new page directory, or 0 on failure.
 */
uint32_t paging_clone_directory(void);

/**
 * free a process page directory.
 * frees any user-space page tables (indices 0–767) and their mapped frames,
 * then frees the directory page itself.
 * does NOT free kernel page tables (indices 768–1023) since those are shared.
 * @param pd_phys physical address of the page directory to free.
 */
void paging_free_directory(uint32_t pd_phys);

/**
 * map a page in a specific page directory (not necessarily the current one).
 * used to set up process address spaces before switching to them.
 * @param pd_phys physical address of the target page directory.
 * @param virt virtual address to map (page-aligned).
 * @param phys physical address to map to (page-aligned).
 * @param flags PTE flags.
 * @return true on success, false on failure.
 */
bool paging_map_in(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * switch the active page directory (load CR3).
 * does not modify CR0 paging must already be enabled.
 * @param pd_phys physical address of the page directory to activate.
 */
void paging_switch_directory(uint32_t pd_phys);
