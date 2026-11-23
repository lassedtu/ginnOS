#include "ktest.h"

#include "arch/arch.h"
#include "common/memory.h"
#include "kernel/memory/fault.h"
#include "kernel/memory/mman.h"
#include "kernel/memory/page.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vma.h"
#include "kernel/process/process.h"

#define COW_ADDR 0x40000000u

/**
 * a process with one writable anonymous page at COW_ADDR, filled with 0xAB.
 * @param frame receives the page's frame.
 */
static process_t *parent_with_page(void **frame)
{
    process_t *proc = process_create();
    if (!proc)
        return NULL;

    *frame = pmm_alloc_page();
    memset(*frame, 0xAB, PAGE_SIZE);
    arch_map_page(proc->page_directory, COW_ADDR, virt_to_phys(*frame), MMU_USER_RW);
    vma_insert(&proc->vmas, COW_ADDR, COW_ADDR + PAGE_SIZE, PROT_READ | PROT_WRITE, VMA_ANON);
    return proc;
}

/**
 * a copy of parent's user half, as fork makes it.
 */
static process_t *copy_of(process_t *parent)
{
    process_t *child = process_create();
    if (!child)
        return NULL;
    arch_copy_user_space(child->page_directory, parent->page_directory);
    child->vmas = parent->vmas;
    return child;
}

KTEST(cow_copy_shares_frames_read_only)
{
    uint32_t free_before = pmm_free_count();
    void *frame;
    process_t *parent = parent_with_page(&frame);
    process_t *child = copy_of(parent);
    KEXPECT(parent && child);

    // one frame, two references, and neither side can write it
    KEXPECT_EQ(pmm_page_refcount(frame), 2);
    KEXPECT_EQ(arch_translate_in(child->page_directory, COW_ADDR), virt_to_phys(frame));
    KEXPECT(arch_user_access_ok(parent->page_directory, COW_ADDR, 1, false));
    KEXPECT(!arch_user_access_ok(parent->page_directory, COW_ADDR, 1, true));
    KEXPECT(!arch_user_access_ok(child->page_directory, COW_ADDR, 1, true));

    process_destroy(child);
    KEXPECT_EQ(pmm_page_refcount(frame), 1);
    process_destroy(parent);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(cow_write_copies_then_reuses)
{
    uint32_t free_before = pmm_free_count();
    void *frame;
    process_t *parent = parent_with_page(&frame);
    process_t *child = copy_of(parent);
    KEXPECT(parent && child);

    // the child writes first: it gets its own copy with the same contents
    KEXPECT(mm_fault_in(child, COW_ADDR + 8, true));
    phys_addr_t child_frame = arch_translate_in(child->page_directory, COW_ADDR);
    KEXPECT(child_frame != virt_to_phys(frame));
    KEXPECT(arch_user_access_ok(child->page_directory, COW_ADDR, 1, true));
    KEXPECT_EQ(((uint8_t *)phys_to_virt(child_frame))[100], 0xAB);
    KEXPECT_EQ(pmm_page_refcount(frame), 1);

    // the parent is now the only user of the old frame: no copy, it only
    // becomes writable again
    KEXPECT(mm_fault_in(parent, COW_ADDR, true));
    KEXPECT_EQ(arch_translate_in(parent->page_directory, COW_ADDR), virt_to_phys(frame));
    KEXPECT(arch_user_access_ok(parent->page_directory, COW_ADDR, 1, true));

    // a read-only area is never made writable
    vma_protect(&child->vmas, COW_ADDR, COW_ADDR + PAGE_SIZE, PROT_READ);
    arch_protect_page(child->page_directory, COW_ADDR, MMU_FLAG_PRESENT | MMU_FLAG_USER);
    KEXPECT(!mm_fault_in(child, COW_ADDR, true));

    process_destroy(child);
    process_destroy(parent);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(count_user_pages_counts_ram_only)
{
    void *frame;
    process_t *parent = parent_with_page(&frame);
    KEXPECT(parent != NULL);
    KEXPECT_EQ(arch_count_user_pages(parent->page_directory), 1);

    // a second page of RAM counts, a device page (no PMM frame) does not
    void *second = pmm_alloc_page();
    arch_map_page(parent->page_directory, COW_ADDR + PAGE_SIZE, virt_to_phys(second),
                  MMU_USER_RW);
    arch_map_page(parent->page_directory, COW_ADDR + 2 * PAGE_SIZE, 0xFE000000u, MMU_USER_RW);
    KEXPECT_EQ(arch_count_user_pages(parent->page_directory), 2);
    arch_unmap_page(parent->page_directory, COW_ADDR + 2 * PAGE_SIZE);

    // the copy shares both pages: each address space counts them
    process_t *child = copy_of(parent);
    KEXPECT(child != NULL);
    KEXPECT_EQ(arch_count_user_pages(child->page_directory), 2);
    process_destroy(child);
    process_destroy(parent);
}
