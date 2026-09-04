#include "paging.h"
#include "isr.h"

#include "common/memory.h"
#include "common/stdio.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pmm_layout.h"
#include "kernel/memory/region.h"
#include "kernel/panic.h"

extern void paging_flush(uint32_t page_directory_phys);
extern void paging_invalidate(uint32_t virtual_address);
extern uint32_t paging_read_cr2(void);
extern uint32_t paging_read_cr0(void);
extern void paging_load_cr3(uint32_t page_directory_phys);

#define PF_PRESENT 0x01u        /* 0 = not-present page, 1 = protection violation */
#define PF_WRITE 0x02u          /* 0 = read access, 1 = write access */
#define PF_USER 0x04u           /* 0 = supervisor mode, 1 = user mode */
#define PF_RESERVED_WRITE 0x08u /* 1 = fault caused by reserved bit set in page entry */
#define PF_INSTRUCTION 0x10u    /* 1 = fault caused by instruction fetch */

/**
 * dedicated page fault handler.
 * decodes CR2 (faulting address) and the error code bits, prints a diagnostic,
 * and panics. in the future this can be extended for demand paging or COW.
 */
static void page_fault_handler(struct registers *regs)
{
    uint32_t faulting_address = paging_read_cr2();
    uint32_t err = regs->error;

    printf("\r\n=== PAGE FAULT ===\r\n");
    printf("faulting address: 0x%x\r\n", faulting_address);
    printf("error code: 0x%x\r\n", err);

    printf("  %s\r\n", (err & PF_PRESENT) ? "protection violation" : "page not present");
    printf("  %s access\r\n", (err & PF_WRITE) ? "write" : "read");
    printf("  %s mode\r\n", (err & PF_USER) ? "user" : "supervisor");

    if (err & PF_RESERVED_WRITE)
    {
        printf("  reserved bit set in page entry\r\n");
    }

    if (err & PF_INSTRUCTION)
    {
        printf("  instruction fetch\r\n");
    }

    printf("eip=0x%x cs=0x%x eflags=0x%x\r\n",
           regs->eip, regs->cs, regs->eflags);

    kernel_panic("page fault");
}

/**
 * pointer to the kernel page directory (virtual == physical while identity-mapped).
 */
static uint32_t *kernel_directory;

/**
 * physical address of the kernel page directory.
 */
static uint32_t kernel_directory_phys;

/**
 * number of page tables allocated.
 */
static uint32_t allocated_tables;

/**
 * allocate a page-aligned, zeroed 4 KiB page from the PMM.
 * panics if allocation fails.
 * @return physical address of the allocated page.
 */
static uint32_t alloc_page_zeroed(void)
{
    void *page = pmm_alloc_page();

    if (!page)
    {
        kernel_panic("paging: out of physical memory");
    }

    memset(page, 0, PAGE_SIZE);
    return (uint32_t)page;
}

void paging_init(void)
{
    uint32_t total_memory;
    uint32_t tables_needed;
    uint32_t i;

    /* determine how much physical memory to identity-map */
    total_memory = pmm_total_pages() * PAGE_SIZE;

    /* we need one page table per 4 MiB of address space */
    tables_needed = (total_memory + (PAGE_SIZE * PAGE_ENTRIES) - 1) / (PAGE_SIZE * PAGE_ENTRIES);

    /* allocate the page directory */
    kernel_directory_phys = alloc_page_zeroed();
    kernel_directory = (uint32_t *)kernel_directory_phys;

    allocated_tables = 0;

    /* allocate page tables and fill them with identity mappings */
    for (i = 0; i < tables_needed; i++)
    {
        uint32_t table_phys = alloc_page_zeroed();
        uint32_t *table = (uint32_t *)table_phys;
        uint32_t j;

        /* fill 1024 entries: each maps a 4 KiB page */
        for (j = 0; j < PAGE_ENTRIES; j++)
        {
            uint32_t phys_addr = (i * PAGE_ENTRIES + j) * PAGE_SIZE;

            /* don't map beyond physical memory */
            if (phys_addr >= total_memory)
            {
                break;
            }

            table[j] = phys_addr | PTE_KERNEL_RW;
        }

        /* install the page table in the page directory */
        kernel_directory[i] = table_phys | PDE_KERNEL_RW;
        allocated_tables++;
    }

    /* reserve page directory + table memory so the PMM won't reuse it */
    region_reserve(kernel_directory_phys,
                   kernel_directory_phys + PAGE_SIZE,
                   "page_directory");

    /* install dedicated page fault handler (replaces generic exception handler) */
    isr_register_handler(14, page_fault_handler);

    /* load CR3 and set CR0.PG paging is now active */
    paging_flush(kernel_directory_phys);
}

bool paging_map(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    uint32_t *table;

    /* if the page table doesn't exist yet, allocate one */
    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        uint32_t new_table = alloc_page_zeroed();
        kernel_directory[dir_index] = new_table | PDE_KERNEL_RW;
        allocated_tables++;
    }

    /* if mapping a user-accessible page, the PDE must also have the user bit
     * set the CPU checks both levels before granting access. */
    if (flags & PTE_USER)
    {
        kernel_directory[dir_index] |= PDE_USER;
    }

    table = (uint32_t *)PAGE_FRAME(kernel_directory[dir_index]);
    table[tbl_index] = (phys & 0xFFFFF000u) | (flags & 0xFFFu);

    paging_invalidate(virt);
    return true;
}

void paging_unmap(uint32_t virt)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    uint32_t *table;

    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        return;
    }

    table = (uint32_t *)PAGE_FRAME(kernel_directory[dir_index]);
    table[tbl_index] = 0;

    paging_invalidate(virt);
}

uint32_t paging_get_physical(uint32_t virt)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    uint32_t *table;

    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        return 0;
    }

    table = (uint32_t *)PAGE_FRAME(kernel_directory[dir_index]);

    if (!(table[tbl_index] & PTE_PRESENT))
    {
        return 0;
    }

    return PAGE_FRAME(table[tbl_index]) | (virt & 0xFFF);
}

uint32_t paging_get_physical_in(uint32_t pd_phys, uint32_t virt)
{
    uint32_t *dir = (uint32_t *)pd_phys;
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    uint32_t *table;

    if (!(dir[dir_index] & PDE_PRESENT))
    {
        return 0;
    }

    table = (uint32_t *)PAGE_FRAME(dir[dir_index]);

    if (!(table[tbl_index] & PTE_PRESENT))
    {
        return 0;
    }

    return PAGE_FRAME(table[tbl_index]) | (virt & 0xFFF);
}

uint32_t paging_directory_address(void)
{
    return kernel_directory_phys;
}

uint32_t paging_table_count(void)
{
    return allocated_tables;
}

bool paging_is_enabled(void)
{
    // bit 31 of CR0 is the paging-enable (PG) flag.
    return (paging_read_cr0() & 0x80000000u) != 0;
}

// index boundary: entries 0–767 are user space, 768–1023 are kernel.
#define KERNEL_PDE_START 768

uint32_t paging_clone_directory(void)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        return 0;
    }

    uint32_t *new_dir = (uint32_t *)page;

    // copy all entries from the kernel directory.
    // kernel page tables are marked supervisor-only (no PDE_USER bit),
    // so user code cannot access kernel memory even though entries are present.
    // user-space mappings (with PDE_USER) will be added per-process.
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        new_dir[i] = kernel_directory[i];
    }

    return (uint32_t)new_dir;
}

void paging_free_directory(uint32_t pd_phys)
{
    if (pd_phys == 0 || pd_phys == kernel_directory_phys)
    {
        return;
    }

    uint32_t *dir = (uint32_t *)pd_phys;

    // free process-specific page tables and their user-mapped frames.
    // only process entries with PDE_USER set  these are either newly
    // allocated or copied-on-write from kernel tables.
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        if (!(dir[i] & PDE_PRESENT))
        {
            continue;
        }

        // skip kernel-shared entries (no user bit)
        if (!(dir[i] & PDE_USER))
        {
            continue;
        }

        uint32_t *table = (uint32_t *)PAGE_FRAME(dir[i]);

        // only free physical frames that have PTE_USER set
        // kernel identity-mapped entries (copied) don't have PTE_USER.
        for (uint32_t j = 0; j < PAGE_ENTRIES; j++)
        {
            if ((table[j] & PTE_PRESENT) && (table[j] & PTE_USER))
            {
                uint32_t frame = PAGE_FRAME(table[j]);
                if (frame != 0)
                {
                    pmm_free_page((void *)frame);
                }
            }
        }

        // free the page table itself (it was allocated for this process)
        pmm_free_page(table);
    }

    // free the directory page
    pmm_free_page((void *)pd_phys);
}

bool paging_map_in(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t *dir = (uint32_t *)pd_phys;
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    uint32_t *table;

    if (!(dir[dir_index] & PDE_PRESENT))
    {
        // no page table exists  allocate a fresh one
        void *new_table = pmm_alloc_page();
        if (!new_table)
        {
            return false;
        }
        memset(new_table, 0, PAGE_SIZE);
        dir[dir_index] = (uint32_t)new_table | PDE_KERNEL_RW;
    }
    else if ((flags & PTE_USER) && !(dir[dir_index] & PDE_USER))
    {
        // PDE exists but is kernel-only (shared from clone).
        // we need to add a user mapping, so we must copy the page table
        // to avoid contaminating the shared kernel table.
        uint32_t *old_table = (uint32_t *)PAGE_FRAME(dir[dir_index]);
        void *new_table = pmm_alloc_page();
        if (!new_table)
        {
            return false;
        }
        // copy existing kernel entries
        memcpy(new_table, old_table, PAGE_SIZE);
        dir[dir_index] = (uint32_t)new_table | PDE_USER_RW;
    }

    // if mapping a user-accessible page, the PDE must also have the user bit
    if (flags & PTE_USER)
    {
        dir[dir_index] |= PDE_USER;
    }

    table = (uint32_t *)PAGE_FRAME(dir[dir_index]);
    table[tbl_index] = (phys & 0xFFFFF000u) | (flags & 0xFFFu);

    return true;
}

void paging_switch_directory(uint32_t pd_phys)
{
    // paging is already enabled here, so only CR3 changes; CR0 is left alone.
    paging_load_cr3(pd_phys);
}
