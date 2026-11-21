#pragma once

#include <sys/types.h>

/**
 * @file sys/wait.h
 * @brief waiting for child processes.
 *
 * the status word and the option values are ABI and must match the kernel
 * side in src/kernel/process/wait_status.h.
 */

#define WNOHANG    1 // return 0 at once if no child has anything to report
#define WUNTRACED  2 // also report children that stopped
#define WCONTINUED 8 // also report children that continued

// decoding a status word from wait/waitpid
#define WIFEXITED(status)    (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xff)
#define WIFSIGNALED(status)  (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)     ((status) & 0x7f)
#define WIFSTOPPED(status)   (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)     (((status) >> 8) & 0xff)
#define WIFCONTINUED(status) ((status) == 0xffff)

/**
 * wait for a child process to exit and reap it.
 * @param pid a child's pid, or -1 for any child.
 * @param status receives the status word (may be NULL).
 * @param options 0 or WNOHANG.
 * @return the reaped child's pid, 0 with WNOHANG if no matching child has
 *         exited yet, or -1 on error (errno set; ECHILD if there is no
 *         matching child).
 */
pid_t waitpid(pid_t pid, int *status, int options);

/**
 * wait for any child to exit and reap it.
 * @param status receives the status word (may be NULL).
 * @return the child's pid, or -1 on error (errno set).
 */
pid_t wait(int *status);
