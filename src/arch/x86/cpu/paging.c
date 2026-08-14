#include "paging.h"

#include "../../../common/stdio.h"
#include "../../../kernel/memory/pmm.h"
#include "../../../kernel/memory/pmm_layout.h"

/**
 * kernel page directory (physical address).
 */
static page_directory_t *kernel_directory;

/**
 * number of page tables allocated so far.
 */
static uint32_t allocated_tables;

void paging_init(void)
{
    kernel_directory = (page_directory_t *)0;
    allocated_tables = 0;

    /* TODO: allocate page directory */
    /* TODO: identity map physical memory */
    /* TODO: install page fault handler */
    /* TODO: load CR3 and enable paging */

    printf("Paging: not yet implemented (structures defined)\r\n");
}

bool paging_map(uint32_t virt, uint32_t phys, uint32_t flags)
{
    (void)virt;
    (void)phys;
    (void)flags;
    return false;
}

void paging_unmap(uint32_t virt)
{
    (void)virt;
}

uint32_t paging_get_physical(uint32_t virt)
{
    (void)virt;
    return 0;
}

uint32_t paging_directory_address(void)
{
    return (uint32_t)kernel_directory;
}

uint32_t paging_table_count(void)
{
    return allocated_tables;
}

bool paging_is_enabled(void)
{
    /* read CR0 and check PG bit (bit 31) */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000u) != 0;
}
