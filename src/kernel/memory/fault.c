#include "fault.h"

#include "mman.h"
#include "page.h"
#include "pmm.h"
#include "vma.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "common/stdio.h"
#include "kernel/klog/klog.h"
#include "kernel/signal/signal.h"
#include "kernel/usermode/usermode.h"


/**
 * the MMU flags for a backed page of an area.
 */
static uint32_t page_flags(const vma_t *area)
{
    uint32_t flags = MMU_FLAG_PRESENT | MMU_FLAG_USER;
    if (area->prot & PROT_WRITE)
    {
        flags |= MMU_FLAG_WRITE;
    }
    return flags;
}

/**
 * give a process its own writable copy of a copy-on-write page. if no other
 * address space shares the frame any more, the page only becomes writable.
 */
static bool break_cow(process_t *proc, const vma_t *area, uintptr_t page, phys_addr_t frame)
{
    void *old = phys_to_virt(frame);

    if (pmm_page_refcount(old) == 1)
    {
        // the last user: nothing to copy
        return arch_protect_page(proc->page_directory, page, page_flags(area));
    }

    void *copy = pmm_alloc_page();
    if (!copy)
    {
        return false;
    }
    memcpy(copy, old, PAGE_SIZE);

    if (arch_map_page(proc->page_directory, page, virt_to_phys(copy), page_flags(area)) != 0)
    {
        pmm_free_page(copy);
        return false;
    }

    // this address space no longer uses the shared frame
    pmm_free_page(old);
    return true;
}

bool mm_fault_in(process_t *proc, uintptr_t addr, bool write)
{
    if (!proc || !proc->page_directory)
    {
        return false;
    }

    vma_t *area = vma_find(&proc->vmas, addr);
    if (!area)
    {
        return false;
    }

    // the area must allow the access; device areas are mapped up front, so
    // a fault inside one is never fixed by backing a page with RAM
    if (write && !(area->prot & PROT_WRITE))
    {
        return false;
    }
    if (!(area->prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) || area->kind == VMA_DEVICE)
    {
        return false;
    }

    uintptr_t page = PAGE_ALIGN_DOWN(addr);
    phys_addr_t frame = arch_translate_in(proc->page_directory, page);
    if (frame != 0)
    {
        // already backed. in a writable area the only page user code
        // cannot write is a copy-on-write page shared after fork
        if (write && !arch_user_access_ok(proc->page_directory, page, 1, true))
        {
            return break_cow(proc, area, page, PAGE_ALIGN_DOWN(frame));
        }
        return false;
    }

    void *new_frame = pmm_alloc_page();
    if (!new_frame)
    {
        return false;
    }
    memset(new_frame, 0, PAGE_SIZE);

    if (arch_map_page(proc->page_directory, page, virt_to_phys(new_frame), page_flags(area)) != 0)
    {
        pmm_free_page(new_frame);
        return false;
    }
    return true;
}

void mm_handle_user_fault(uintptr_t addr, bool write, bool present, uintptr_t pc)
{
    process_t *proc = process_current();

    // a missing page is backed on demand; a write to a present page may be
    // a copy-on-write page
    if ((!present || write) && mm_fault_in(proc, addr, write))
    {
        return; // fixed: retry the instruction
    }

    uint32_t pid = proc ? proc->pid : 0;
    KLOG_WARN("pid %u: segmentation fault: %s 0x%lx at pc 0x%lx", pid,
              write ? "write to" : "read of", addr, pc);

    // the signal is acted on when the fault handler returns to user mode:
    // a handler can run, else the process dies with a report
    if (proc)
    {
        proc->fault_addr = addr;
        proc->fault_write = write;
        signal_force(proc, SIGSEGV);
    }
}
