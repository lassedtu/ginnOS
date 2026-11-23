/**
 * @file session.c
 * @brief Bifrost: the desktop as the user who logged in.
 *
 * a child takes the user's group and id, their home and a clean
 * environment, and runs asgard (the desktop shell) in a session of its
 * own. Bifrost waits for it. when asgard ends (Log out), the session's
 * other programs (its process group, and all the user's processes) are
 * asked to end (SIGTERM), and ended (SIGKILL) a moment later, so no window
 * of the session stays over the login screen.
 */

#include "bifrost.h"

#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHELL_PROGRAM "/bin/asgard"
#define END_WAIT_MS   1500

/** send a signal to every process of uid (from /proc) */
static int signal_user(uid_t uid, int sig)
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return 0;
    int sent = 0;
    struct dirent *d;
    while ((d = readdir(dir)))
    {
        char *end;
        long pid = strtol(d->d_name, &end, 10);
        if (*end || pid <= 1 || pid == getpid())
            continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/status", pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        char line[128];
        long owner = -1;
        while (fgets(line, sizeof(line), f))
            if (strncmp(line, "Uid:", 4) == 0)
                owner = strtol(line + 4, NULL, 10);
        fclose(f);
        if (owner == (long)uid && kill((pid_t)pid, sig) == 0)
            sent++;
    }
    closedir(dir);
    return sent;
}

static void start_shell(const struct passwd *pw)
{
    setsid();
    if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
        _exit(126);
    if (chdir(pw->pw_dir) < 0)
        chdir("/");
    // a clean environment: nothing of root's goes to the user
    char home[160];
    char user[80];
    char logname[80];
    char shell[160];
    snprintf(home, sizeof(home), "HOME=%s", pw->pw_dir);
    snprintf(user, sizeof(user), "USER=%s", pw->pw_name);
    snprintf(logname, sizeof(logname), "LOGNAME=%s", pw->pw_name);
    snprintf(shell, sizeof(shell), "SHELL=%s", pw->pw_shell);
    char *envp[] = {home, user, logname, shell, "PATH=/bin", NULL};
    char *argv[] = {"asgard", NULL};
    execve(SHELL_PROGRAM, argv, envp);
    _exit(127);
}

void session_run(const char *name)
{
    struct passwd *found = getpwnam(name);
    if (!found)
        return;
    struct passwd pw = *found;
    char dir[128];
    char shell[128];
    char user[64];
    snprintf(dir, sizeof(dir), "%s", found->pw_dir);
    snprintf(shell, sizeof(shell), "%s", found->pw_shell);
    snprintf(user, sizeof(user), "%s", found->pw_name);
    pw.pw_dir = dir;
    pw.pw_shell = shell;
    pw.pw_name = user;

    pid_t child = fork();
    if (child < 0)
        return;
    if (child == 0)
        start_shell(&pw);
    int status;
    while (waitpid(child, &status, 0) < 0)
    {
    }

    // what the session left: its process group (what asgard started), and
    // for a user other than root every process of theirs (root's own are
    // not all touched: Bifrost is root too)
    int left = kill(-child, SIGTERM) == 0;
    if (pw.pw_uid != 0)
        left += signal_user(pw.pw_uid, SIGTERM);
    if (left)
    {
        usleep(END_WAIT_MS * 1000);
        kill(-child, SIGKILL);
        if (pw.pw_uid != 0)
            signal_user(pw.pw_uid, SIGKILL);
    }
    while (waitpid(-1, &status, WNOHANG) > 0)
    {
    }
}
