/**
 * /bin/waitpid_test  test program for SYS_exec + SYS_waitpid.
 * spawns /bin/hello as a child process, waits for it, and reports
 * the exit code.
 */

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("waitpid_test: starting (pid=%d)\n", getpid());

    /* spawn /bin/hello as a child */
    printf("waitpid_test: spawning /bin/hello...\n");
    const char *argv[] = {"hello", (const char *)0};
    pid_t child_pid = exec("/bin/hello", argv);

    if (child_pid < 0)
    {
        printf("waitpid_test: FAIL  exec returned error\n");
        return 1;
    }

    printf("waitpid_test: child pid=%d\n", child_pid);

    /* wait for the child */
    printf("waitpid_test: waiting for child...\n");
    int exit_code = waitpid(child_pid);

    printf("waitpid_test: child exited with code=%d\n", exit_code);

    if (exit_code == 0)
    {
        printf("waitpid_test: PASS\n");
    }
    else
    {
        printf("waitpid_test: FAIL  unexpected exit code\n");
        return 1;
    }

    return 0;
}
