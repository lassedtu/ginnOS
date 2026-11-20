#include "kstack.h"

#include "memory_layout.h"
#include "pmm.h"
#include "arch/arch.h"
#include "common/memory.h"
#include "kernel/process/process.h"
#include "kernel/sync/spinlock.h"

// bytes per slot: the guard page, then the stack
#define KSTACK_SLOT_SIZE (KSTACK_GUARD_SIZE + KERNEL_STACK_SIZE)

// one slot per possible process
#define KSTACK_SLOTS PROCESS_MAX

#define KSTACK_PAGES (KERNEL_STACK_SIZE / PAGE_SIZE)

#define KSTACK_REGION_END (KSTACK_REGION_BASE + KSTACK_SLOTS * KSTACK_SLOT_SIZE)

// which slots hold a stack (guarded by slot_lock)
static bool slot_used[KSTACK_SLOTS];
static spinlock_t slot_lock;

/**
 * lowest stack address (just above the guard page) of a slot.
 */
static uint32_t slot_base(uint32_t slot)
{
    return KSTACK_REGION_BASE + slot * KSTACK_SLOT_SIZE + KSTACK_GUARD_SIZE;
}

/**
 * unmap the first `pages` pages of a stack and free their frames.
 */
static void unmap_pages(uint32_t base, uint32_t pages)
{
    addr_space_t kernel_as = arch_kernel_address_space();

    for (uint32_t i = 0; i < pages; i++)
    {
        uint32_t virt = base + i * PAGE_SIZE;
        uint32_t phys = arch_translate_in(kernel_as, virt);
        arch_unmap_kernel_page(virt);
        if (phys)
        {
            pmm_free_page((void *)phys);
        }
    }
}

/**
 * return a slot to the free set.
 */
static void release_slot(uint32_t slot)
{
    uint32_t flags = spin_lock_irqsave(&slot_lock);
    slot_used[slot] = false;
    spin_unlock_irqrestore(&slot_lock, flags);
}

void kstack_init(void)
{
    spinlock_init(&slot_lock);
    for (uint32_t i = 0; i < KSTACK_SLOTS; i++)
    {
        slot_used[i] = false;
    }

    arch_reserve_kernel_range(KSTACK_REGION_BASE, KSTACK_REGION_END);
}

uint32_t kstack_alloc(void)
{
    uint32_t slot = KSTACK_SLOTS;

    uint32_t flags = spin_lock_irqsave(&slot_lock);
    for (uint32_t i = 0; i < KSTACK_SLOTS; i++)
    {
        if (!slot_used[i])
        {
            slot_used[i] = true;
            slot = i;
            break;
        }
    }
    spin_unlock_irqrestore(&slot_lock, flags);

    if (slot == KSTACK_SLOTS)
    {
        return 0;
    }

    // the slot is ours: map its stack pages (the guard page stays unmapped)
    uint32_t base = slot_base(slot);
    for (uint32_t i = 0; i < KSTACK_PAGES; i++)
    {
        void *frame = pmm_alloc_page();
        if (!frame)
        {
            unmap_pages(base, i);
            release_slot(slot);
            return 0;
        }
        arch_map_kernel_page(base + i * PAGE_SIZE, (uint32_t)frame);
    }

    memset((void *)base, 0, KERNEL_STACK_SIZE);
    return base;
}

void kstack_free(uint32_t base)
{
    uint32_t slot = (base - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE;
    if (base < KSTACK_REGION_BASE || slot >= KSTACK_SLOTS || base != slot_base(slot))
    {
        return;
    }

    unmap_pages(base, KSTACK_PAGES);
    release_slot(slot);
}

bool kstack_is_guard(uint32_t addr)
{
    if (addr < KSTACK_REGION_BASE || addr >= KSTACK_REGION_END)
    {
        return false;
    }

    return (addr - KSTACK_REGION_BASE) % KSTACK_SLOT_SIZE < KSTACK_GUARD_SIZE;
}
