#pragma once

#include "common/stdint.h"
#include "arch/arch.h"
#include "common/error.h"

struct process;
typedef struct process process_t;

struct registers;

// most argv and envp entries a new program gets
#define USER_ARGC_MAX 64
#define USER_ENVC_MAX 64

/**
 * back the first pages of the user stack in an address space and put a new
 * program's arguments on it, laid out the way crt0 reads them:
 * [esp] 0 (fake return address), argc, argv, envp, then the two
 * NULL-terminated pointer arrays and the strings.
 * the address space must be the active one: the stack is written through
 * its user addresses.
 * @param as the address space.
 * @param argv NULL-terminated argument strings (may be NULL).
 * @param envp NULL-terminated "KEY=VALUE" strings (may be NULL).
 * @return the initial user stack pointer, or 0 if memory ran out.
 */
uintptr_t usermode_build_stack(addr_space_t as, const char **argv, const char **envp);

/**
 * start a new process's program: build its user stack (usermode_build_stack),
 * point the TSS at the current kernel stack and iret to ring 3. the address
 * space must be the active one. never returns.
 * @param entry the program's entry point.
 * @param as the process's address space.
 * @param argv NULL-terminated argument strings (may be NULL).
 * @param envp NULL-terminated environment strings (may be NULL).
 */
void jump_to_usermode(uintptr_t entry, addr_space_t as, const char **argv, const char **envp)
    __attribute__((noreturn));

/**
 * make a syscall frame return into a fresh program: clear the general
 * registers and set the user entry point, stack, flags and segments.
 * @param regs the syscall frame.
 * @param entry the program's entry point.
 * @param user_esp the initial user stack pointer.
 */
void usermode_reset_frame(struct registers *regs, uintptr_t entry, uintptr_t user_esp);

/**
 * replace the current process's program (execve). the new image is loaded
 * into a new address space first, so on failure the caller keeps running
 * its old program. on success the old address space is gone, the areas
 * and break are the new program's, its stack holds argv and envp, and the
 * syscall frame returns into its entry point. pid, parent, cwd and fds stay
 * (except FD_CLOEXEC ones, which are closed).
 * @param regs the caller's syscall frame.
 * @param path absolute path of the ELF executable.
 * @param argv NULL-terminated argument strings in kernel memory.
 * @param envp NULL-terminated environment strings in kernel memory.
 * @return KERR_OK, KERR_NOENT if the program cannot be loaded, KERR_PERM
 *         if the caller may not run it, or KERR_NOMEM.
 */
kerr_t exec_replace(struct registers *regs, const char *path, const char **argv,
                    const char **envp);

/**
 * start a program as a new process. creates the process, loads the ELF at
 * path into its address space, copies argv, inherits the caller's cwd and
 * its fds without FD_CLOEXEC (when called from a process) and queues it on
 * the scheduler. with no current process (boot), the new process has no
 * parent: PID 1.
 *
 * @param path absolute path to the ELF executable.
 * @param argv null-terminated array of argument strings (may be NULL).
 * @return the new process's PID, or -1 on failure.
 */
int exec_program(const char *path, const char **argv);

/**
 * called by SYS_exit: the current process becomes a zombie for its parent
 * to reap, and never runs again. its wait status says it exited with
 * exit_code. if it has no parent (PID 1), the system halts.
 * @param exit_code the exit code passed by the user program (low 8 bits).
 */
void usermode_exit(int exit_code) __attribute__((noreturn));

/**
 * end the current process as killed by a signal: like usermode_exit, but
 * the wait status names the signal (SIGSEGV for a bad memory access).
 * @param signal the signal number (kernel/signal/signal.h).
 */
void usermode_kill(int signal) __attribute__((noreturn));

/**
 * get the current program break (end of the user heap).
 * @return current break address.
 */
uintptr_t usermode_get_brk(void);

/**
 * set the program break.
 * called by exec_program after ELF loading to initialize the break.
 * @param brk the initial break address (page-aligned end of loaded segments).
 */
void usermode_set_brk(uintptr_t brk);

/**
 * set up a child process's kernel stack so that context_switch
 * returns into the process entry trampoline.
 * @param child pointer to the child process control block.
 * @param entry ELF entry point for the child.
 */
void setup_child_stack(process_t *child, uintptr_t entry);

