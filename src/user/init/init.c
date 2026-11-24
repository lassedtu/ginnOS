/**
 * @file init.c
 * @brief PID 1: start the login screen (or the shell), start it again
 *        when it exits, and reap every process whose parent has exited.
 *
 * /etc/ginnos/boot.conf chooses: [boot] session = login (the default)
 * runs Bifrost, the login screen ("heimdalld -x bifrost"), when there is
 * a display; session = text runs the shell on the console. Bifrost does
 * not end by itself, and heimdalld -x ends with its exit status: a login
 * screen that ends with a status other than 0 cannot run. init says so
 * and runs the shell instead, so the computer is never left with nothing.
 *
 * for each network card (the interfaces in /proc/net but lo), init starts
 * "dhcp -b <interface>", which gets an address and keeps it.
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
#define DHCP  "/bin/dhcp"

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

/** start the DHCP client for each network interface but the loopback one */
static void start_network(void)
{
    FILE *f = fopen("/proc/net", "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        size_t length = strcspn(line, "\t ");
        if (length == 0 || length >= 16 || strncmp(line, "lo", length) == 0)
            continue;
        char name[16];
        memcpy(name, line, length);
        name[length] = '\0';
        if (fork() == 0)
        {
            char *argv[] = {"dhcp", "-b", name, NULL};
            char *envp[] = {"PATH=/bin", NULL};
            execve(DHCP, argv, envp);
            _exit(127);
        }
    }
    fclose(f);
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

    start_network();
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
            bool failed = pid == front && !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            if (login && (failed || front < 0))
            {
                printf("init: the login screen cannot run; this is the text console\n");
                login = false;
            }
            else if (time(NULL) - started < QUICK_EXIT_SECONDS)
            {
                sleep(1); // do not spin if the shell cannot start
            }
            front = login ? start_login() : start_shell();
            started = time(NULL);
        }
        // any other pid: an orphan init adopted, now reaped
    }
}
