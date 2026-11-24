#include "user_space.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "common/memory.h"

user_space_t *user_space_create(void)
{
    user_space_t *space = kmalloc(sizeof(user_space_t));
    if (!space)
    {
        return NULL;
    }
    memset(space, 0, sizeof(*space));
    space->page_directory = arch_create_address_space();
    if (space->page_directory == ADDR_SPACE_NONE)
    {
        kfree(space);
        return NULL;
    }
    vma_table_init(&space->vmas);
    mutex_init(&space->lock);
    space->users = 1;
    return space;
}

user_space_t *user_space_get(user_space_t *space)
{
    uint32_t flags = arch_irq_save();
    space->users++;
    arch_irq_restore(flags);
    return space;
}

void user_space_put(user_space_t *space)
{
    if (!space)
    {
        return;
    }
    uint32_t flags = arch_irq_save();
    bool last = --space->users == 0;
    process_t *current = process_current();
    if (last && current && current->space == space)
    {
        // the kernel stacks are in the kernel half, so they stay mapped
        arch_switch_address_space(arch_kernel_address_space());
    }
    arch_irq_restore(flags);
    if (last)
    {
        arch_destroy_address_space(space->page_directory);
        kfree(space);
    }
}
