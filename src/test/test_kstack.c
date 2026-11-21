#include "ktest.h"

#include "arch/arch.h"
#include "kernel/memory/kstack.h"
#include "kernel/memory/pmm.h"
#include "kernel/process/process.h"

KTEST(kstack_alloc_maps_stack_not_guard)
{
    uint32_t free_before = pmm_free_count();

    uintptr_t base = kstack_alloc();
    KEXPECT(base != 0);
    if (!base)
        return;

    addr_space_t kernel_as = arch_kernel_address_space();

    // every stack page is mapped, zeroed and writable
    KEXPECT(arch_translate_in(kernel_as, base) != 0);
    KEXPECT(arch_translate_in(kernel_as, base + KERNEL_STACK_SIZE - 1) != 0);
    volatile uint8_t *stack = (volatile uint8_t *)base;
    KEXPECT_EQ(stack[0], 0);
    KEXPECT_EQ(stack[KERNEL_STACK_SIZE - 1], 0);
    stack[KERNEL_STACK_SIZE - 1] = 0x5A;
    KEXPECT_EQ(stack[KERNEL_STACK_SIZE - 1], 0x5A);

    // the page just below is the unmapped guard
    KEXPECT(kstack_is_guard(base - 1));
    KEXPECT(kstack_is_guard(base - KSTACK_GUARD_SIZE));
    KEXPECT(!kstack_is_guard(base));
    KEXPECT(!kstack_is_guard(base + KERNEL_STACK_SIZE - 1));
    KEXPECT_EQ(arch_translate_in(kernel_as, base - 1), 0);

    kstack_free(base);
    KEXPECT_EQ(arch_translate_in(kernel_as, base), 0);
    KEXPECT_EQ(pmm_free_count(), free_before);
}

KTEST(kstack_slots_exhaust_and_reuse)
{
    uintptr_t bases[PROCESS_MAX];
    uint32_t count = 0;

    // the test kernel runs no processes, so every slot is free
    while (count < PROCESS_MAX)
    {
        bases[count] = kstack_alloc();
        if (!bases[count])
            break;
        count++;
    }
    KEXPECT_EQ(count, PROCESS_MAX);
    KEXPECT_EQ(kstack_alloc(), 0);

    // distinct, non-overlapping stacks
    KEXPECT(bases[1] - bases[0] >= KERNEL_STACK_SIZE + KSTACK_GUARD_SIZE);

    kstack_free(bases[0]);
    uintptr_t again = kstack_alloc();
    KEXPECT_EQ(again, bases[0]);

    for (uint32_t i = 0; i < count; i++)
        kstack_free(bases[i]);
}
