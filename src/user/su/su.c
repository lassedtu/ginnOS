/**
 * @file su.c
 * @brief Run a shell (or a command) as another user: su <user> [command...]
 *
 * there are no passwords yet, so only root may use it.
 */

#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: su <user> [command [args...]]\n");
        return 1;
    }
    if (geteuid() != 0)
    {
        printf("su: only root can switch users (there are no passwords)\n");
        return 1;
    }

    struct passwd *user = getpwnam(argv[1]);
    if (!user)
    {
        printf("su: unknown user '%s'\n", argv[1]);
        return 1;
    }

    // the group first: setuid gives up the right to change it
    if (setgid(user->pw_gid) < 0 || setuid(user->pw_uid) < 0)
    {
        perror("su");
        return 1;
    }
    chdir(user->pw_dir);

    char *shell_argv[] = {user->pw_shell, NULL};
    char **run = argc > 2 ? argv + 2 : shell_argv;

    // a command without a slash is looked for in /bin
    char path[128];
    if (!strchr(run[0], '/'))
    {
        snprintf(path, sizeof(path), "/bin/%s", run[0]);
        execve(path, run, environ);
    }
    execve(run[0], run, environ);
    perror(run[0]);
    return 127;
}
