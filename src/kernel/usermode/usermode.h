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

/**
 * load and execute an ELF binary from the filesystem.
 * loads the ELF at the given path, allocates a user stack, and jumps
 * to the entry point in ring 3. returns when the program calls SYS_exit.
 *
 * @param path absolute path to the ELF executable.
 * @return exit code from the user program, or -1 on load failure.
 */
int exec_program(const char *path);

/**
 * called by SYS_exit to return control to the caller of exec_program.
 * restores the saved kernel context and resumes after jump_to_usermode.
 * @param exit_code the exit code passed by the user program.
 */
void usermode_exit(int exit_code);
