#pragma once

#include "common/stdint.h"

struct process;
typedef struct process process_t;

/**
 * jump to a user-mode function at the given entry point.
 * allocates a user stack, pushes argc/argv onto it, configures the TSS
 * for ring 0 return, and performs an iret to ring 3.
 *
 * this function does not return control continues in userspace
 * until a syscall (e.g. SYS_exit) brings it back to the kernel.
 *
 * @param entry virtual address of the user function to execute.
 * @param pd_phys physical address of the process's page directory.
 * @param argv null-terminated array of argument strings (may be NULL).
 */
void jump_to_usermode(uint32_t entry, uint32_t pd_phys, const char **argv);

/**
 * load and execute an ELF binary from the filesystem.
 * loads the ELF at the given path, allocates a user stack, pushes
 * argc/argv, and jumps to the entry point in ring 3.
 * returns when the program calls SYS_exit.
 *
 * @param path absolute path to the ELF executable.
 * @param argv null-terminated array of argument strings (may be NULL).
 * @return exit code from the user program, or -1 on load failure.
 */
int exec_program(const char *path, const char **argv);

/**
 * called by SYS_exit to return control to the caller of exec_program.
 * restores the saved kernel context and resumes after jump_to_usermode.
 * @param exit_code the exit code passed by the user program.
 */
void usermode_exit(int exit_code);

/**
 * get the current program break (end of the user heap).
 * @return current break address.
 */
uint32_t usermode_get_brk(void);

/**
 * set the program break.
 * called by exec_program after ELF loading to initialize the break.
 * @param brk the initial break address (page-aligned end of loaded segments).
 */
void usermode_set_brk(uint32_t brk);

/**
 * set up a child process's kernel stack so that context_switch
 * returns into the process entry trampoline.
 * @param child pointer to the child process control block.
 * @param entry ELF entry point for the child.
 */
void setup_child_stack(process_t *child, uint32_t entry);

