/**
 * 32-bit (non-PAE) paging for the i686 kernel: a page directory and page
 * tables of 1024 entries. the x86_64 kernel uses paging64.c.
 */

#ifndef __x86_64__

#include "paging.h"
#include "exception.h"
#include "isr.h"

#include "common/memory.h"
#include "common/stdio.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pmm_layout.h"
#include "kernel/memory/region.h"
#include "kernel/panic.h"
#include "kernel/memory/fault.h"
#include "kernel/memory/kstack.h"
#include "kernel/assert.h"
#include "arch/x86/cpu/gdt.h"

extern void paging_flush(uint32_t page_directory_phys);
extern void paging_invalidate(uintptr_t virtual_address);
extern uint32_t paging_read_cr2(void);

// first page directory entry of the kernel half (KERNEL_VIRT_BASE)
#define KERNEL_PDE_START PAGE_DIR_INDEX(KERNEL_VIRT_BASE)
extern uint32_t paging_read_cr0(void);
extern void paging_load_cr3(uint32_t page_directory_phys);
extern uint32_t paging_read_cr3(void);

#define PF_PRESENT 0x01u        /* 0 = not-present page, 1 = protection violation */
#define PF_WRITE 0x02u          /* 0 = read access, 1 = write access */
#define PF_USER 0x04u           /* 0 = supervisor mode, 1 = user mode */
#define PF_RESERVED_WRITE 0x08u /* 1 = fault caused by reserved bit set in page entry */
#define PF_INSTRUCTION 0x10u    /* 1 = fault caused by instruction fetch */

/**
 * dedicated page fault handler.
 * a fault raised by user code goes to the memory manager, which backs the
 * page on demand or kills the process. a kernel fault decodes CR2 and the
 * error code bits, prints a diagnostic, and panics.
 */
static void page_fault_handler(struct registers *regs)
{
    uint32_t faulting_address = paging_read_cr2();
    uint32_t err = regs->error;

    if (err & PF_USER)
    {
        mm_handle_user_fault(faulting_address, (err & PF_WRITE) != 0, (err & PF_PRESENT) != 0,
                             regs->eip);
        return;
    }

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

    exception_dump_regs(regs);

    // a supervisor access into a kernel stack's guard page means the stack
    // overflowed (pushes that hit it become double faults instead)
    if (!(err & PF_USER) && kstack_is_guard(faulting_address))
    {
        exception_panic("kernel stack overflow", regs);
    }

    exception_panic("page fault", regs);
}

/**
 * pointer to the kernel page directory (virtual == physical while identity-mapped).
 */
static pte_t *kernel_directory;

/**
 * physical address of the kernel page directory.
 */
static phys_addr_t kernel_directory_phys;

/**
 * number of page tables allocated.
 */
static uint32_t allocated_tables;

/**
 * set by the first paging_clone_directory(); from then on a new kernel page
 * table would not be seen by existing address spaces.
 */
static bool directory_cloned;

/**
 * allocate a page-aligned, zeroed 4 KiB page from the PMM.
 * panics if allocation fails.
 * @return physical address of the allocated page.
 */
static phys_addr_t alloc_page_zeroed(void)
{
    void *page = pmm_alloc_page();

    if (!page)
    {
        kernel_panic("paging: out of physical memory");
    }

    memset(page, 0, PAGE_SIZE);
    return virt_to_phys(page);
}

void paging_init(void)
{
    // RAM the direct map covers (the PMM already capped it at PHYS_MAP_SIZE)
    phys_addr_t total_memory = (phys_addr_t)pmm_total_pages() * PAGE_SIZE;

    /* allocate the page directory */
    kernel_directory_phys = alloc_page_zeroed();
    kernel_directory = (pte_t *)phys_to_virt(kernel_directory_phys);

    allocated_tables = 0;

    // every kernel-half PDE gets its page table now. clones copy these
    // entries, so the tables are shared by every address space and a kernel
    // mapping made later is seen everywhere. the user half stays empty: there
    // is no identity map, so a physical address used as a pointer faults.
    for (uint32_t dir_index = KERNEL_PDE_START; dir_index < PAGE_ENTRIES; dir_index++)
    {
        kernel_directory[dir_index] = (pte_t)alloc_page_zeroed() | PDE_KERNEL_RW;
        allocated_tables++;
    }

    // the direct map: physical address P at virtual P + KERNEL_VIRT_BASE
    for (phys_addr_t phys = 0; phys < total_memory; phys += PAGE_SIZE)
    {
        uintptr_t virt = (uintptr_t)phys_to_virt(phys);
        pte_t *table = entry_table(kernel_directory[PAGE_DIR_INDEX(virt)]);
        table[PAGE_TABLE_INDEX(virt)] = (pte_t)phys | PTE_KERNEL_RW;
    }

    /* reserve page directory + table memory so the PMM won't reuse it */
    region_reserve(kernel_directory_phys,
                   kernel_directory_phys + PAGE_SIZE,
                   "page_directory");

    /* the double-fault task switches CR3 to the kernel directory */
    tss_set_double_fault_cr3((uint32_t)kernel_directory_phys);

    /* install dedicated page fault handler (replaces generic exception handler) */
    isr_register_handler(14, page_fault_handler);

    /* paging is already on (the entry stub's boot directory): switch to the
     * kernel directory; the boot identity map is gone from here on */
    paging_load_cr3((uint32_t)kernel_directory_phys);
}

bool paging_map(uintptr_t virt, phys_addr_t phys, uint32_t flags)
{
    KASSERT(virt >= KERNEL_VIRT_BASE, "paging_map: not a kernel address");

    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    pte_t *table;

    /* if the page table doesn't exist yet, allocate one */
    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        phys_addr_t new_table = alloc_page_zeroed();
        kernel_directory[dir_index] = (pte_t)new_table | PDE_KERNEL_RW;
        allocated_tables++;
    }

    /* if mapping a user-accessible page, the PDE must also have the user bit
     * set the CPU checks both levels before granting access. */
    if (flags & PTE_USER)
    {
        kernel_directory[dir_index] |= PDE_USER;
    }

    table = entry_table(kernel_directory[dir_index]);
    table[tbl_index] = (pte_t)(PAGE_ALIGN_DOWN(phys) | (flags & PAGE_OFFSET_MASK));

    paging_invalidate(virt);
    return true;
}

void paging_unmap(uintptr_t virt)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    pte_t *table;

    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        return;
    }

    table = entry_table(kernel_directory[dir_index]);
    table[tbl_index] = 0;

    paging_invalidate(virt);
}

phys_addr_t paging_get_physical(uintptr_t virt)
{
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    pte_t *table;

    if (!(kernel_directory[dir_index] & PDE_PRESENT))
    {
        return 0;
    }

    table = entry_table(kernel_directory[dir_index]);

    if (!(table[tbl_index] & PTE_PRESENT))
    {
        return 0;
    }

    return PAGE_FRAME(table[tbl_index]) | (virt & PAGE_OFFSET_MASK);
}

phys_addr_t paging_get_physical_in(phys_addr_t pd_phys, uintptr_t virt)
{
    pte_t *dir = (pte_t *)phys_to_virt(pd_phys);
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    pte_t *table;

    if (!(dir[dir_index] & PDE_PRESENT))
    {
        return 0;
    }

    table = entry_table(dir[dir_index]);

    if (!(table[tbl_index] & PTE_PRESENT))
    {
        return 0;
    }

    return PAGE_FRAME(table[tbl_index]) | (virt & PAGE_OFFSET_MASK);
}

phys_addr_t paging_directory_address(void)
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

void paging_reserve_tables(uintptr_t start, uintptr_t end)
{
    KASSERT(!directory_cloned, "paging_reserve_tables after an address space was cloned");

    for (uint32_t dir_index = PAGE_DIR_INDEX(start); dir_index <= PAGE_DIR_INDEX(end - 1);
         dir_index++)
    {
        if (!(kernel_directory[dir_index] & PDE_PRESENT))
        {
            kernel_directory[dir_index] = (pte_t)alloc_page_zeroed() | PDE_KERNEL_RW;
            allocated_tables++;
        }
    }
}

bool paging_user_range_ok(phys_addr_t pd_phys, uintptr_t addr, size_t len, bool write)
{
    if (len == 0)
    {
        return true;
    }

    uintptr_t last = addr + len - 1;
    if (last < addr)
    {
        return false; // wraps around the address space
    }

    const pte_t *dir = (const pte_t *)phys_to_virt(pd_phys);
    uint32_t pde_need = PDE_PRESENT | PDE_USER | (write ? PDE_READ_WRITE : 0);
    uint32_t pte_need = PTE_PRESENT | PTE_USER | (write ? PTE_READ_WRITE : 0);

    for (uintptr_t page = PAGE_ALIGN_DOWN(addr);; page += PAGE_SIZE)
    {
        pte_t pde = dir[PAGE_DIR_INDEX(page)];
        if ((pde & pde_need) != pde_need)
        {
            return false;
        }

        const pte_t *table = entry_table(pde);
        pte_t pte = table[PAGE_TABLE_INDEX(page)];
        if ((pte & pte_need) != pte_need)
        {
            return false;
        }

        if (page >= PAGE_ALIGN_DOWN(last))
        {
            return true;
        }
    }
}

uintptr_t paging_fault_address(void)
{
    return paging_read_cr2();
}

phys_addr_t paging_clone_directory(void)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        return 0;
    }

    pte_t *new_dir = (pte_t *)page;
    directory_cloned = true;

    // the user half starts empty; the kernel half shares the kernel's page
    // tables (supervisor-only: no PDE_USER, so user code cannot reach them)
    for (uint32_t i = 0; i < KERNEL_PDE_START; i++)
    {
        new_dir[i] = 0;
    }
    for (uint32_t i = KERNEL_PDE_START; i < PAGE_ENTRIES; i++)
    {
        new_dir[i] = kernel_directory[i];
    }

    return virt_to_phys(new_dir);
}

void paging_free_directory(phys_addr_t pd_phys)
{
    if (pd_phys == 0 || pd_phys == kernel_directory_phys)
    {
        return;
    }

    pte_t *dir = (pte_t *)phys_to_virt(pd_phys);

    // free the process's own page tables (the user half) and the user frames
    // mapped in them. the kernel half is shared and never freed here.
    for (uint32_t i = 0; i < KERNEL_PDE_START; i++)
    {
        if (!(dir[i] & PDE_PRESENT))
        {
            continue;
        }

        pte_t *table = entry_table(dir[i]);

        // every frame mapped in the user half belongs to the process, also
        // a page made supervisor-only by mprotect(PROT_NONE). a shared
        // (copy-on-write) frame only loses this process's reference.
        for (uint32_t j = 0; j < PAGE_ENTRIES; j++)
        {
            if (table[j] & PTE_PRESENT)
            {
                phys_addr_t frame = PAGE_FRAME(table[j]);
                // skip device mappings (e.g. a framebuffer mapped into the
                // process): those frames aren't PMM RAM and must not be freed.
                if (frame != 0 && pmm_owns(frame))
                {
                    pmm_free_page(phys_to_virt(frame));
                }
            }
        }

        // free the page table itself (it was allocated for this process)
        pmm_free_page(table);
    }

    // free the directory page
    pmm_free_page(phys_to_virt(pd_phys));
}

bool paging_map_in(phys_addr_t pd_phys, uintptr_t virt, phys_addr_t phys, uint32_t flags)
{
    pte_t *dir = (pte_t *)phys_to_virt(pd_phys);
    uint32_t dir_index = PAGE_DIR_INDEX(virt);
    uint32_t tbl_index = PAGE_TABLE_INDEX(virt);
    pte_t *table;

    // only the user half is per-process; the kernel half's tables are shared
    KASSERT(virt < KERNEL_VIRT_BASE, "paging_map_in: kernel address");

    if (!(dir[dir_index] & PDE_PRESENT))
    {
        // no page table exists yet: allocate a fresh one for this process
        void *new_table = pmm_alloc_page();
        if (!new_table)
        {
            return false;
        }
        memset(new_table, 0, PAGE_SIZE);
        dir[dir_index] = (pte_t)virt_to_phys(new_table) | PDE_KERNEL_RW;
    }

    // if mapping a user-accessible page, the PDE must also have the user bit
    if (flags & PTE_USER)
    {
        dir[dir_index] |= PDE_USER;
    }

    table = entry_table(dir[dir_index]);
    table[tbl_index] = (pte_t)(PAGE_ALIGN_DOWN(phys) | (flags & PAGE_OFFSET_MASK));

    // the entry can replace a present one (copy-on-write): drop a cached
    // translation of the old one
    paging_invalidate(virt);
    return true;
}

bool paging_copy_user(phys_addr_t dst_phys, phys_addr_t src_phys)
{
    pte_t *src = (pte_t *)phys_to_virt(src_phys);
    pte_t *dst = (pte_t *)phys_to_virt(dst_phys);

    for (uint32_t i = 0; i < KERNEL_PDE_START; i++)
    {
        if (!(src[i] & PDE_PRESENT))
        {
            continue;
        }

        void *table_page = pmm_alloc_page();
        if (!table_page)
        {
            return false; // the caller frees dst, which drops its references
        }
        memset(table_page, 0, PAGE_SIZE);
        dst[i] = (pte_t)virt_to_phys(table_page) | (src[i] & PAGE_OFFSET_MASK);

        pte_t *src_table = entry_table(src[i]);
        pte_t *dst_table = (pte_t *)table_page;
        for (uint32_t j = 0; j < PAGE_ENTRIES; j++)
        {
            pte_t pte = src_table[j];
            if (!(pte & PTE_PRESENT))
            {
                continue;
            }

            phys_addr_t frame = PAGE_FRAME(pte);
            if (pmm_owns(frame) && (pte & PTE_SHARED))
            {
                // shared memory: both processes keep writing the same frame
                pmm_page_ref(phys_to_virt(frame));
            }
            else if (pmm_owns(frame))
            {
                // RAM: both processes share the frame read-only. the first
                // write in either one copies it (mm_fault_in).
                pte &= ~(pte_t)PTE_READ_WRITE;
                src_table[j] = pte;
                pmm_page_ref(phys_to_virt(frame));
            }
            // device memory is shared as it is
            dst_table[j] = pte;
        }
    }

    // the source lost write access to its pages: drop its cached entries
    // if it is the live directory
    if (paging_read_cr3() == src_phys)
    {
        paging_load_cr3((uint32_t)src_phys);
    }
    return true;
}

/**
 * the user PTE for virt in a page directory, or NULL if no page table covers
 * it.
 */
static pte_t *user_pte(phys_addr_t pd_phys, uintptr_t virt)
{
    KASSERT(virt < KERNEL_VIRT_BASE, "user_pte: kernel address");

    pte_t *dir = (pte_t *)phys_to_virt(pd_phys);
    pte_t pde = dir[PAGE_DIR_INDEX(virt)];
    if (!(pde & PDE_PRESENT))
    {
        return NULL;
    }
    return &entry_table(pde)[PAGE_TABLE_INDEX(virt)];
}

phys_addr_t paging_unmap_in(phys_addr_t pd_phys, uintptr_t virt)
{
    pte_t *pte = user_pte(pd_phys, virt);
    if (!pte || !(*pte & PTE_PRESENT))
    {
        return 0;
    }

    phys_addr_t frame = PAGE_FRAME(*pte);
    *pte = 0;

    // the mapping may be cached if pd_phys is the live directory. invlpg on
    // another directory only drops an unrelated entry, which is harmless.
    paging_invalidate(virt);
    return frame;
}

bool paging_protect_in(phys_addr_t pd_phys, uintptr_t virt, uint32_t flags)
{
    pte_t *pte = user_pte(pd_phys, virt);
    if (!pte || !(*pte & PTE_PRESENT))
    {
        return false;
    }

    *pte = (pte_t)(PAGE_FRAME(*pte) | (flags & PAGE_OFFSET_MASK));
    paging_invalidate(virt);
    return true;
}

void paging_switch_directory(phys_addr_t pd_phys)
{
    // paging is already enabled here, so only CR3 changes; CR0 is left alone.
    paging_load_cr3((uint32_t)pd_phys);
}

#endif // !__x86_64__
