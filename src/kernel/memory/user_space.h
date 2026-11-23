#pragma once

/**
 * the user half of an address space: the page tables, the areas (vma.h)
 * and the program break. the threads of a process use one user space
 * together; it is freed when the last of them lets it go.
 */

#include "common/stdint.h"
#include "kernel/memory/vma.h"
#include "kernel/sync/mutex.h"
#include "arch/arch.h"

typedef struct user_space
{
    uint32_t users;              // the processes (threads) that use it
    addr_space_t page_directory; // the page tables (the kernel half is shared)
    vma_table_t vmas;            // what is mapped in the user half
    uintptr_t brk;               // the program break (for sbrk)
    mutex_t lock;                // held by a memory syscall (mmap, sbrk, fork) of a thread
} user_space_t;

/**
 * make an empty user space with one user.
 * @return the space, or NULL if there is no memory.
 */
user_space_t *user_space_create(void);

/**
 * add a user to a space (a new thread that shares it).
 * @param space the space.
 * @return space.
 */
user_space_t *user_space_get(user_space_t *space);

/**
 * remove a user from a space. the last user frees it: its pages, its page
 * tables and the object. if the current process uses the space, the CPU
 * moves to the kernel's page tables first; the caller then sets its own
 * space pointer to NULL.
 * @param space the space, or NULL (nothing is done).
 */
void user_space_put(user_space_t *space);
