#include "mm.h"

#include "memory_layout.h"
#include "mman.h"
#include "page.h"
#include "pmm.h"
#include "vma.h"
#include "arch/arch.h"

/**
 * true if [start, end) is a non-empty, page-aligned range of user space.
 */
static bool valid_user_range(uintptr_t start, uintptr_t end)
{
    return start < end && PAGE_IS_ALIGNED(start) && PAGE_IS_ALIGNED(end) &&
           start >= USER_SPACE_START && end <= USER_SPACE_END;
}

/**
 * the MMU flags that give user code the access PROT_* bits describe.
 */
static uint32_t mmu_flags_for(uint32_t prot)
{
    if (!(prot & (PROT_READ | PROT_WRITE | PROT_EXEC)))
    {
        // mapped, but supervisor-only: user code faults on it
        return MMU_FLAG_PRESENT;
    }
    uint32_t flags = MMU_FLAG_PRESENT | MMU_FLAG_USER;
    if (prot & PROT_WRITE)
    {
        flags |= MMU_FLAG_WRITE;
    }
    return flags;
}

kerr_t mm_unmap(process_t *proc, uintptr_t start, uintptr_t end)
{
    if (!valid_user_range(start, end))
    {
        return KERR_INVAL;
    }

    // a hole punched in the middle of an area needs a free slot: check
    // before touching any page, so failure leaves everything mapped
    vma_t *inside = vma_find(&proc->vmas, start);
    if (inside && inside->start < start && inside->end > end &&
        proc->vmas.count == VMA_MAX)
    {
        return KERR_NOSPC;
    }

    for (uint32_t i = 0; i < proc->vmas.count; i++)
    {
        vma_t *area = &proc->vmas.areas[i];
        uintptr_t from = area->start > start ? area->start : start;
        uintptr_t to = area->end < end ? area->end : end;

        for (uintptr_t page = from; page < to; page += PAGE_SIZE)
        {
            phys_addr_t frame = arch_unmap_page(proc->page_directory, page);
            // device memory (e.g. a mapped framebuffer) is not the PMM's
            if (frame != 0 && pmm_owns(frame))
            {
                pmm_free_page(phys_to_virt(frame));
            }
        }
    }

    return vma_remove(&proc->vmas, start, end);
}

kerr_t mm_protect(process_t *proc, uintptr_t start, uintptr_t end, uint32_t prot)
{
    if (!valid_user_range(start, end))
    {
        return KERR_INVAL;
    }

    kerr_t err = vma_protect(&proc->vmas, start, end, prot);
    if (kerr_failed(err))
    {
        return err;
    }

    // pages not backed yet are skipped: they get the new protection when
    // they are faulted in
    uint32_t flags = mmu_flags_for(prot);
    for (uintptr_t page = start; page < end; page += PAGE_SIZE)
    {
        uint32_t page_flags = flags;
        phys_addr_t frame = arch_translate_in(proc->page_directory, page);

        vma_t *area = vma_find(&proc->vmas, page);
        if (area && area->kind == VMA_SHARED)
        {
            // shared memory keeps its mark; its frames are meant to have
            // more than one user
            page_flags |= MMU_FLAG_SHARED;
        }
        else if (frame != 0 && pmm_owns(frame) &&
                 pmm_page_refcount(phys_to_virt(PAGE_ALIGN_DOWN(frame))) > 1)
        {
            // a frame shared copy-on-write stays read-only: the first
            // write copies it (mm_fault_in)
            page_flags &= ~MMU_FLAG_WRITE;
        }
        arch_protect_page(proc->page_directory, page, page_flags);
    }
    return KERR_OK;
}
