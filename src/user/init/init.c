/**
 * @file init.c
 * @brief PID 1: start the login screen (or the shell), start it again
 *        when it exits, and reap every process whose parent has exited.
 *
 * /etc/ginnos/boot.conf chooses: [boot] session = login (the default)
 * runs Bifrost, the login screen ("heimdalld -x bifrost"), when there is
 * a display; session = text runs the shell on the console. a login screen
 * that ends within LOGIN_FAIL_SECONDS of its start cannot run: init says
 * so and runs the shell instead, so the computer is never left with
 * nothing.
 *
 * the kernel gives orphaned processes to init, so init waits for any
 * child, not only the shell. the shell runs in a session of its own with
 * the console as its controlling terminal.
 */

#include <conf.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHELL "/bin/sh"
#define LOGIN "/bin/heimdalld"

// a login screen that ends this soon after its start cannot run
#define LOGIN_FAIL_SECONDS 5

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
    char *envp[] = {"PATH=/bin", "HOME=/root", NULL};
    execve(SHELL, argv, envp);
    printf("init: cannot run %s\n", SHELL);
    _exit(127);
}

static pid_t start_login(void)
{
    pid_t pid = fork();
    if (pid != 0)
        return pid;
    setsid();
    char *argv[] = {"heimdalld", "-x", "bifrost", NULL};
    char *envp[] = {"PATH=/bin", "HOME=/root", NULL};
    execve(LOGIN, argv, envp);
    _exit(127);
}

/** true when the login screen is chosen and there is a display for it */
static bool want_login(void)
{
    conf_t *conf = conf_open("boot");
    bool login = !conf || strcmp(conf_get(conf, "boot", "session", "login"), "text") != 0;
    conf_free(conf);
    return login && access("/dev/display", F_OK) == 0;
}

int main(void)
{
    if (getpid() != 1)
    {
        printf("init: must run as process 1\n");
        return 1;
    }

    bool login = want_login();
    pid_t front = login ? start_login() : start_shell(); // the login screen or the shell
    time_t started = time(NULL);

    for (;;)
    {
        int status;
        pid_t pid = wait(&status);
        if (pid < 0)
        {
            if (errno == ECHILD)
            {
                // nothing at all runs: start it again
                front = -1;
            }
            else
            {
                continue;
            }
        }

        if (pid == front || front < 0)
        {
            bool quick = time(NULL) - started < (login ? LOGIN_FAIL_SECONDS : QUICK_EXIT_SECONDS);
            if (login && quick)
            {
                printf("init: the login screen cannot run; this is the text console\n");
                login = false;
            }
            else if (quick)
            {
                sleep(1); // do not spin if the shell cannot start
            }
            front = login ? start_login() : start_shell();
            started = time(NULL);
        }
        // any other pid: an orphan init adopted, now reaped
    }
}
