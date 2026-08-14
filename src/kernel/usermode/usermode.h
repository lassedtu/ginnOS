#pragma once

#include "../../common/stdint.h"

/**
 * jump to a user-mode function at the given entry point.
 * allocates a user stack, configures the TSS for ring 0 return,
 * and performs an iret to ring 3.
 *
 * this function does not return — control continues in userspace
 * until a syscall (e.g. SYS_exit) brings it back to the kernel.
 *
 * @param entry virtual address of the user function to execute.
 */
void jump_to_usermode(uint32_t entry);
