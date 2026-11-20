#include "fault.h"

#include "mman.h"
#include "page.h"
#include "pmm.h"
#include "vma.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "common/stdio.h"
#include "kernel/klog/klog.h"
#include "kernel/usermode/usermode.h"

// exit status of a process killed by a bad memory access (128 + SIGSEGV)
#define SEGFAULT_EXIT_STATUS 139

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

    // already backed: a protection fault on a page the area allows is not
    // something demand paging fixes (copy-on-write comes later)
    uintptr_t page = PAGE_ALIGN_DOWN(addr);
    if (arch_translate_in(proc->page_directory, page) != 0)
    {
        return false;
    }

    void *frame = pmm_alloc_page();
    if (!frame)
    {
        return false;
    }
    memset(frame, 0, PAGE_SIZE);

    uint32_t flags = MMU_FLAG_PRESENT | MMU_FLAG_USER;
    if (area->prot & PROT_WRITE)
    {
        flags |= MMU_FLAG_WRITE;
    }

    if (arch_map_page(proc->page_directory, page, virt_to_phys(frame), flags) != 0)
    {
        pmm_free_page(frame);
        return false;
    }
    return true;
}

void mm_handle_user_fault(uintptr_t addr, bool write, bool present, uintptr_t pc)
{
    process_t *proc = process_current();

    if (!present && mm_fault_in(proc, addr, write))
    {
        return; // backed: retry the instruction
    }

    uint32_t pid = proc ? proc->pid : 0;
    KLOG_WARN("pid %u: segmentation fault: %s 0x%lx at pc 0x%lx", pid,
              write ? "write to" : "read of", addr, pc);
    printf("pid %u: segmentation fault (%s 0x%lx)\r\n", pid, write ? "write to" : "read of",
           addr);

    // exiting may sleep (closing files): leave the fault's interrupt gate
    // state behind. this process never returns to the faulting instruction.
    arch_enable_interrupts();
    usermode_exit(SEGFAULT_EXIT_STATUS);
}
