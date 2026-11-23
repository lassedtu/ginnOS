/**
 * 4-level paging for the x86_64 kernel: PML4, PDPT, PD and PT, 512 entries
 * of 64 bits each. the i686 kernel uses paging.c, with the same interface
 * (paging.h).
 *
 * the kernel half is PML4 entries 256 to 511. paging_init gives each of them
 * a PDPT, and every address space copies these 256 entries, so the kernel
 * tables are shared and a kernel mapping made later is seen everywhere. the
 * user half (entries 0 to 255) belongs to one address space.
 *
 * the direct map uses 2 MiB pages. the kernel image (at KERNEL_IMAGE_BASE),
 * the kernel stacks and the MMIO window use 4 KiB pages.
 */

#ifdef __x86_64__

#include "paging.h"
#include "exception.h"
#include "isr.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "common/stdio.h"
#include "kernel/assert.h"
#include "kernel/memory/fault.h"
#include "kernel/memory/kernel_layout.h"
#include "kernel/memory/kstack.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pmm_layout.h"
#include "kernel/memory/region.h"
#include "kernel/panic.h"

#define KERNEL_PML4_START 256u
#define HUGE_PAGE_SIZE    0x200000ul

#define PF_PRESENT        0x01u /* 0 = not-present page, 1 = protection violation */
#define PF_WRITE          0x02u /* 0 = read access, 1 = write access */
#define PF_USER           0x04u /* 0 = supervisor mode, 1 = user mode */
#define PF_RESERVED_WRITE 0x08u /* 1 = a reserved bit is set in a paging entry */
#define PF_INSTRUCTION    0x10u /* 1 = the access was an instruction fetch */

/* the end of the RAM the bootloader reports (limine_boot.c); the direct map
 * covers at least this */
phys_addr_t boot_direct_map_end;

static pte_t *kernel_pml4;
static phys_addr_t kernel_pml4_phys;
static uint32_t allocated_tables;
static bool directory_cloned;

static inline void invalidate(uintptr_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static inline phys_addr_t read_cr3(void)
{
    phys_addr_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void load_cr3(phys_addr_t value)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static inline uintptr_t read_cr2(void)
{
    uintptr_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

/**
 * a zeroed page for a paging structure, or 0 if there is no memory.
 */
static phys_addr_t alloc_table(void)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        return 0;
    }
    memset(page, 0, PAGE_SIZE);
    allocated_tables++;
    return virt_to_phys(page);
}

static phys_addr_t alloc_table_or_panic(void)
{
    phys_addr_t table = alloc_table();
    if (!table)
    {
        kernel_panic("paging: out of physical memory");
    }
    return table;
}

/**
 * the entry for virt at the PT level, or NULL. with create, the missing
 * tables are made; with user, the PML4, PDPT and PD entries on the way get
 * the user bit (the CPU checks it at every level). a 2 MiB or 1 GiB page on
 * the way gives NULL: those are only in the direct map.
 */
static pte_t *walk(pte_t *pml4, uintptr_t virt, bool create, bool user)
{
    pte_t *table = pml4;
    uint32_t shifts[3] = {39, 30, 21};

    for (int level = 0; level < 3; level++)
    {
        pte_t *entry = &table[(virt >> shifts[level]) & 0x1FF];
        if (!(*entry & PTE_PRESENT))
        {
            if (!create)
            {
                return NULL;
            }
            phys_addr_t next = alloc_table();
            if (!next)
            {
                return NULL;
            }
            *entry = next | PDE_KERNEL_RW;
        }
        else if (*entry & PTE_HUGE)
        {
            return NULL;
        }

        if (user)
        {
            *entry |= PDE_USER;
        }
        table = entry_table(*entry);
    }
    return &table[PT_INDEX(virt)];
}

/**
 * the physical address behind virt, following 1 GiB and 2 MiB pages too,
 * or 0 if it is not mapped.
 */
static phys_addr_t translate(const pte_t *pml4, uintptr_t virt)
{
    pte_t entry = pml4[PML4_INDEX(virt)];
    if (!(entry & PTE_PRESENT))
    {
        return 0;
    }

    entry = entry_table(entry)[PDPT_INDEX(virt)];
    if (!(entry & PTE_PRESENT))
    {
        return 0;
    }
    if (entry & PTE_HUGE)
    {
        return (PAGE_FRAME(entry) & ~0x3FFFFFFFul) | (virt & 0x3FFFFFFFul);
    }

    entry = entry_table(entry)[PD_INDEX(virt)];
    if (!(entry & PTE_PRESENT))
    {
        return 0;
    }
    if (entry & PTE_HUGE)
    {
        return (PAGE_FRAME(entry) & ~(HUGE_PAGE_SIZE - 1)) | (virt & (HUGE_PAGE_SIZE - 1));
    }

    entry = entry_table(entry)[PT_INDEX(virt)];
    if (!(entry & PTE_PRESENT))
    {
        return 0;
    }
    return PAGE_FRAME(entry) | (virt & PAGE_OFFSET_MASK);
}

/**
 * the page fault handler. a fault in user code goes to the memory manager,
 * which backs the page on demand or kills the process. a kernel fault is
 * printed and the kernel panics.
 */
static void page_fault_handler(struct registers *regs)
{
    // the page fault vector is an interrupt gate: CR2 is read before an
    // interrupt can switch to a process whose own fault changes it. then
    // interrupts are as they were before the fault
    uintptr_t address = read_cr2();
    uint32_t err = (uint32_t)regs->error;
    if (REG_FLAGS(regs) & EFLAGS_IF)
    {
        arch_enable_interrupts();
    }

    if (err & PF_USER)
    {
        mm_handle_user_fault(address, (err & PF_WRITE) != 0, (err & PF_PRESENT) != 0,
                             REG_IP(regs));
        return;
    }

    printf("\r\n=== PAGE FAULT ===\r\n");
    printf("faulting address: 0x%lx\r\n", address);
    printf("error code: 0x%x\r\n", err);
    printf("  %s\r\n", (err & PF_PRESENT) ? "protection violation" : "page not present");
    printf("  %s access\r\n", (err & PF_WRITE) ? "write" : "read");
    if (err & PF_RESERVED_WRITE)
    {
        printf("  reserved bit set in a paging entry\r\n");
    }
    if (err & PF_INSTRUCTION)
    {
        printf("  instruction fetch\r\n");
    }

    exception_dump_regs(regs);

    if (kstack_is_guard(address))
    {
        exception_panic("kernel stack overflow", regs);
    }
    exception_panic("page fault", regs);
}

/**
 * map the direct map: physical [0, end) at KERNEL_VIRT_BASE, with 2 MiB
 * pages.
 */
static void map_direct(phys_addr_t end)
{
    for (phys_addr_t phys = 0; phys < end; phys += HUGE_PAGE_SIZE)
    {
        uintptr_t virt = (uintptr_t)phys_to_virt(phys);
        pte_t *pdpt = entry_table(kernel_pml4[PML4_INDEX(virt)]);
        pte_t *pdpt_entry = &pdpt[PDPT_INDEX(virt)];
        if (!(*pdpt_entry & PTE_PRESENT))
        {
            *pdpt_entry = alloc_table_or_panic() | PDE_KERNEL_RW;
        }
        pte_t *pd = entry_table(*pdpt_entry);
        pd[PD_INDEX(virt)] = phys | PTE_KERNEL_RW | PTE_HUGE;
    }
}

/**
 * map the kernel image at KERNEL_IMAGE_BASE with 4 KiB pages.
 */
static void map_kernel_image(void)
{
    uintptr_t start = PAGE_ALIGN_DOWN(kernel_start_address());
    uintptr_t end = PAGE_ALIGN_UP(kernel_end_address());

    for (uintptr_t virt = start; virt < end; virt += PAGE_SIZE)
    {
        pte_t *pte = walk(kernel_pml4, virt, true, false);
        if (!pte)
        {
            kernel_panic("paging: out of physical memory");
        }
        *pte = virt_to_phys((void *)virt) | PTE_KERNEL_RW;
    }
}

void paging_init(void)
{
    kernel_pml4_phys = alloc_table_or_panic();
    kernel_pml4 = (pte_t *)phys_to_virt(kernel_pml4_phys);

    // every kernel-half PML4 entry gets its PDPT now, so the kernel tables
    // are shared by all address spaces (paging_clone_directory copies these
    // entries). the user half stays empty.
    for (uint32_t i = KERNEL_PML4_START; i < PAGE_ENTRIES; i++)
    {
        kernel_pml4[i] = alloc_table_or_panic() | PDE_KERNEL_RW;
    }

    phys_addr_t end = (phys_addr_t)pmm_total_pages() * PAGE_SIZE;
    if (boot_direct_map_end > end)
    {
        end = boot_direct_map_end;
    }
    if (end > PHYS_MAP_SIZE)
    {
        end = PHYS_MAP_SIZE;
    }
    map_direct(PAGE_ALIGN_UP(end));
    map_kernel_image();

    region_reserve(kernel_pml4_phys, kernel_pml4_phys + PAGE_SIZE, "page_directory");
    isr_register_handler(14, page_fault_handler);

    // from here on the kernel's own tables are used, not the bootloader's
    load_cr3(kernel_pml4_phys);
}

bool paging_map(uintptr_t virt, phys_addr_t phys, uint32_t flags)
{
    KASSERT(virt >= KERNEL_VIRT_BASE, "paging_map: not a kernel address");

    pte_t *pte = walk(kernel_pml4, virt, true, (flags & PTE_USER) != 0);
    if (!pte)
    {
        return false;
    }
    *pte = PAGE_ALIGN_DOWN(phys) | (flags & PAGE_OFFSET_MASK);
    invalidate(virt);
    return true;
}

void paging_unmap(uintptr_t virt)
{
    pte_t *pte = walk(kernel_pml4, virt, false, false);
    if (pte)
    {
        *pte = 0;
        invalidate(virt);
    }
}

phys_addr_t paging_get_physical(uintptr_t virt)
{
    // the live tables: the kernel half is the same in all of them
    return translate((const pte_t *)phys_to_virt(read_cr3()), virt);
}

phys_addr_t paging_get_physical_in(phys_addr_t pd_phys, uintptr_t virt)
{
    return translate((const pte_t *)phys_to_virt(pd_phys), virt);
}

phys_addr_t paging_directory_address(void)
{
    return kernel_pml4_phys;
}

uint32_t paging_table_count(void)
{
    return allocated_tables;
}

bool paging_is_enabled(void)
{
    return true; // long mode always has paging on
}

void paging_reserve_tables(uintptr_t start, uintptr_t end)
{
    KASSERT(!directory_cloned, "paging_reserve_tables after an address space was cloned");

    // make the tables down to the PT level for each 2 MiB of the range, so
    // a later mapping in it never changes a shared upper-level entry
    for (uintptr_t virt = start & ~(HUGE_PAGE_SIZE - 1); virt < end; virt += HUGE_PAGE_SIZE)
    {
        if (!walk(kernel_pml4, virt, true, false))
        {
            kernel_panic("paging: out of physical memory");
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
    if (last < addr || last >= USER_SPACE_END)
    {
        return false;
    }

    pte_t *pml4 = (pte_t *)phys_to_virt(pd_phys);
    pte_t need = PTE_PRESENT | PTE_USER | (write ? PTE_READ_WRITE : 0);

    for (uintptr_t page = PAGE_ALIGN_DOWN(addr);; page += PAGE_SIZE)
    {
        // the upper levels are checked on the way down
        const pte_t *table = pml4;
        uint32_t shifts[3] = {39, 30, 21};
        for (int level = 0; level < 3; level++)
        {
            pte_t entry = table[(page >> shifts[level]) & 0x1FF];
            if ((entry & need) != need || (entry & PTE_HUGE))
            {
                return false;
            }
            table = entry_table(entry);
        }
        if ((table[PT_INDEX(page)] & need) != need)
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
    return read_cr2();
}

phys_addr_t paging_clone_directory(void)
{
    void *page = pmm_alloc_page();
    if (!page)
    {
        return 0;
    }

    pte_t *pml4 = (pte_t *)page;
    directory_cloned = true;

    // the user half starts empty; the kernel half shares the kernel's tables
    // (no user bit, so user code cannot reach them)
    for (uint32_t i = 0; i < KERNEL_PML4_START; i++)
    {
        pml4[i] = 0;
    }
    for (uint32_t i = KERNEL_PML4_START; i < PAGE_ENTRIES; i++)
    {
        pml4[i] = kernel_pml4[i];
    }
    return virt_to_phys(pml4);
}

/**
 * free a user table and everything under it. level 0 is a PDPT, 1 a PD,
 * 2 a PT. the frames mapped in a PT lose this address space's reference
 * (device memory is not PMM memory and is not freed).
 */
static void free_user_table(pte_t *table, int level)
{
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        pte_t entry = table[i];
        if (!(entry & PTE_PRESENT))
        {
            continue;
        }

        if (level < 2)
        {
            free_user_table(entry_table(entry), level + 1);
            continue;
        }

        phys_addr_t frame = PAGE_FRAME(entry);
        if (frame != 0 && pmm_owns(frame))
        {
            pmm_free_page(phys_to_virt(frame));
        }
    }
    pmm_free_page(table);
}

/** the present RAM pages under a user table; level 0 is a PDPT, 2 a PT */
static uint32_t count_user_table(const pte_t *table, int level)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        pte_t entry = table[i];
        if (!(entry & PTE_PRESENT))
        {
            continue;
        }
        if (level < 2)
        {
            count += count_user_table(entry_table(entry), level + 1);
        }
        else if (pmm_owns(PAGE_FRAME(entry)))
        {
            count++; // RAM only: a device mapping is not the process's memory
        }
    }
    return count;
}

uint32_t paging_count_user(phys_addr_t pd_phys)
{
    if (pd_phys == 0 || pd_phys == kernel_pml4_phys)
    {
        return 0;
    }
    const pte_t *pml4 = (const pte_t *)phys_to_virt(pd_phys);
    uint32_t count = 0;
    for (uint32_t i = 0; i < KERNEL_PML4_START; i++)
    {
        if (pml4[i] & PTE_PRESENT)
        {
            count += count_user_table(entry_table(pml4[i]), 0);
        }
    }
    return count;
}

void paging_free_directory(phys_addr_t pd_phys)
{
    if (pd_phys == 0 || pd_phys == kernel_pml4_phys)
    {
        return;
    }

    pte_t *pml4 = (pte_t *)phys_to_virt(pd_phys);
    for (uint32_t i = 0; i < KERNEL_PML4_START; i++)
    {
        if (pml4[i] & PTE_PRESENT)
        {
            free_user_table(entry_table(pml4[i]), 0);
        }
    }
    pmm_free_page(pml4);
}

bool paging_map_in(phys_addr_t pd_phys, uintptr_t virt, phys_addr_t phys, uint32_t flags)
{
    // only the user half is per address space; the kernel half is shared
    KASSERT(virt < USER_SPACE_END, "paging_map_in: kernel address");

    pte_t *pte = walk((pte_t *)phys_to_virt(pd_phys), virt, true, (flags & PTE_USER) != 0);
    if (!pte)
    {
        return false;
    }
    *pte = PAGE_ALIGN_DOWN(phys) | (flags & PAGE_OFFSET_MASK);

    // the entry can replace a present one (copy-on-write)
    invalidate(virt);
    return true;
}

/**
 * copy a user table for fork. level 0 is a PDPT, 1 a PD, 2 a PT. at the PT
 * level, RAM pages become read-only in both tables and the frame is shared
 * (copy-on-write); device memory is shared as it is.
 * @return the new table's physical address, or 0 if there is no memory.
 */
static phys_addr_t copy_user_table(pte_t *src, int level)
{
    phys_addr_t dst_phys = alloc_table();
    if (!dst_phys)
    {
        return 0;
    }
    pte_t *dst = (pte_t *)phys_to_virt(dst_phys);

    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        pte_t entry = src[i];
        if (!(entry & PTE_PRESENT))
        {
            continue;
        }

        if (level < 2)
        {
            phys_addr_t child = copy_user_table(entry_table(entry), level + 1);
            if (!child)
            {
                // keep what is copied: the caller frees the new tree
                return dst_phys;
            }
            dst[i] = child | (entry & PAGE_OFFSET_MASK);
            continue;
        }

        phys_addr_t frame = PAGE_FRAME(entry);
        if (pmm_owns(frame) && (entry & PTE_SHARED))
        {
            // shared memory: both processes keep writing the same frame
            pmm_page_ref(phys_to_virt(frame));
        }
        else if (pmm_owns(frame))
        {
            // copy-on-write: read-only in both until the first write
            entry &= ~(pte_t)PTE_READ_WRITE;
            src[i] = entry;
            pmm_page_ref(phys_to_virt(frame));
        }
        dst[i] = entry;
    }
    return dst_phys;
}

/**
 * true if every present entry of src (down to the PTs) has a copy in dst.
 */
static bool copy_complete(const pte_t *src, const pte_t *dst, int level)
{
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        if (!(src[i] & PTE_PRESENT))
        {
            continue;
        }
        if (!(dst[i] & PTE_PRESENT))
        {
            return false;
        }
        if (level < 2 && !copy_complete(entry_table(src[i]), entry_table(dst[i]), level + 1))
        {
            return false;
        }
    }
    return true;
}

bool paging_copy_user(phys_addr_t dst_phys, phys_addr_t src_phys)
{
    pte_t *src = (pte_t *)phys_to_virt(src_phys);
    pte_t *dst = (pte_t *)phys_to_virt(dst_phys);
    bool ok = true;

    for (uint32_t i = 0; i < KERNEL_PML4_START && ok; i++)
    {
        if (!(src[i] & PTE_PRESENT))
        {
            continue;
        }
        phys_addr_t copy = copy_user_table(entry_table(src[i]), 0);
        if (!copy)
        {
            ok = false;
            break;
        }
        dst[i] = copy | (src[i] & PAGE_OFFSET_MASK);
        ok = copy_complete(entry_table(src[i]), entry_table(dst[i]), 0);
    }

    // the source lost write access to its pages: drop its cached entries
    if (read_cr3() == src_phys)
    {
        load_cr3(src_phys);
    }
    return ok;
}

/**
 * the user PTE for virt, or NULL if no table covers it.
 */
static pte_t *user_pte(phys_addr_t pd_phys, uintptr_t virt)
{
    KASSERT(virt < USER_SPACE_END, "user_pte: kernel address");
    return walk((pte_t *)phys_to_virt(pd_phys), virt, false, false);
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
    invalidate(virt);
    return frame;
}

bool paging_protect_in(phys_addr_t pd_phys, uintptr_t virt, uint32_t flags)
{
    pte_t *pte = user_pte(pd_phys, virt);
    if (!pte || !(*pte & PTE_PRESENT))
    {
        return false;
    }

    *pte = PAGE_FRAME(*pte) | (flags & PAGE_OFFSET_MASK);
    invalidate(virt);
    return true;
}

void paging_switch_directory(phys_addr_t pd_phys)
{
    load_cr3(pd_phys);
}

#endif // __x86_64__
