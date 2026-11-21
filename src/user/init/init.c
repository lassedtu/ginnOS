/**
 * @file init.c
 * @brief PID 1: start the login shell, start it again when it exits, and
 *        reap every process whose parent has exited.
 *
 * the kernel gives orphaned processes to init, so init waits for any
 * child, not only the shell. the shell runs in a session of its own with
 * the console as its controlling terminal.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHELL "/bin/sh"

// a shell that ends this soon after it started waits before the next start
#define QUICK_EXIT_SECONDS 2

static pid_t start_shell(void)
{
    pid_t pid = fork();
    if (pid != 0)
        return pid;

    // a new session that takes the terminal from init's
    setsid();
    ioctl(0, TIOCSCTTY, (void *)1);

    char *argv[] = {"sh", NULL};
    char *envp[] = {"PATH=/bin", "HOME=/", NULL};
    execve(SHELL, argv, envp);
    printf("init: cannot run %s\n", SHELL);
    _exit(127);
}

int main(void)
{
    if (getpid() != 1)
    {
        printf("init: must run as process 1\n");
        return 1;
    }

    pid_t shell = start_shell();
    time_t started = time(NULL);

    for (;;)
    {
        int status;
        pid_t pid = wait(&status);
        if (pid < 0)
        {
            if (errno == ECHILD)
            {
                // nothing at all runs: start the shell again
                shell = -1;
            }
            else
            {
                continue;
            }
        }

        if (pid == shell || shell < 0)
        {
            if (time(NULL) - started < QUICK_EXIT_SECONDS)
                sleep(1); // do not spin if the shell cannot start
            shell = start_shell();
            started = time(NULL);
        }
        // any other pid: an orphan init adopted, now reaped
    }
}
