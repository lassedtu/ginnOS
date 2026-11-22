/**
 * @file apps.c
 * @brief list the programs of the desktop, or start one.
 *
 * usage: apps            list the programs (/usr/share/applications)
 *        apps -a         also the hidden ones
 *        apps run <id>   start the program <id> and wait for it
 */

#include "apps.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int list(const app_list_t *apps, bool all)
{
    for (int i = 0; i < apps->count; i++)
    {
        const app_t *app = &apps->apps[i];
        if (app->hidden && !all)
            continue;
        printf("%-12s %-20s %-12s %s\n", app->id, app->name, app->category, app->exec);
    }
    return 0;
}

static int run(const app_list_t *apps, const char *id)
{
    const app_t *app = apps_find(apps, id);
    if (!app)
    {
        printf("apps: no program %s\n", id);
        return 1;
    }
    char buffer[256];
    char *argv[APP_ARGS_MAX + 1];
    if (strlen(app->exec) >= sizeof(buffer) || app_split_exec(app->exec, buffer, argv) < 0)
    {
        printf("apps: %s: bad exec line\n", id);
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0)
    {
        execve(argv[0], argv, environ);
        // a name without a path is in /bin
        char path[128];
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
        execve(path, argv, environ);
        printf("apps: cannot start %s\n", argv[0]);
        _exit(127);
    }
    if (pid < 0)
    {
        printf("apps: cannot start %s\n", argv[0]);
        return 1;
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(int argc, char **argv)
{
    app_list_t *apps = apps_load(APPS_DIR);
    if (!apps)
    {
        printf("apps: out of memory\n");
        return 1;
    }
    int result;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-a") == 0))
        result = list(apps, argc == 2);
    else if (argc == 3 && strcmp(argv[1], "run") == 0)
        result = run(apps, argv[2]);
    else
    {
        printf("usage: apps [-a] | apps run <id>\n");
        result = 1;
    }
    apps_free(apps);
    return result;
}
