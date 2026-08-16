/**
 * @file execute.c
 * @brief Command execution for the shell.
 *
 * This file implements the command execution logic for the shell. It handles executing commands either directly or by searching the PATH environment variable for the command.
 */

#include "execute.h"
#include "env.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PATH_BUF_SIZE 256

// external — set by main.c after each command
extern int shell_last_exit_code;

/**
 * check if a path contains a slash (indicating it's a direct path,
 * not a bare command name to look up in PATH).
 */
static int has_slash(const char *s)
{
    while (*s)
    {
        if (*s == '/')
            return 1;
        s++;
    }
    return 0;
}

/**
 * try to exec a program at the given path with the given argv.
 * @return child PID on success, -1 on failure.
 */
static int try_exec(const char *path, const char **argv)
{
    return (int)exec(path, argv);
}

/**
 * search PATH for the command and execute it.
 * PATH is colon-separated (e.g. "/bin:/usr/bin").
 */
static int exec_with_path(const char *cmd, const char **argv)
{
    const char *path_var = env_get("PATH");
    if (!path_var || path_var[0] == '\0')
    {
        // no PATH set — try /bin as fallback
        path_var = "/bin";
    }

    // iterate through colon-separated directories
    const char *p = path_var;
    char full_path[PATH_BUF_SIZE];

    while (*p)
    {
        // extract next directory
        int len = 0;
        while (*p && *p != ':' && len < PATH_BUF_SIZE - 2)
        {
            full_path[len++] = *p;
            p++;
        }

        // skip the colon
        if (*p == ':')
            p++;

        // append / and command name
        if (len > 0 && full_path[len - 1] != '/')
            full_path[len++] = '/';

        int cmd_len = strlen(cmd);
        if (len + cmd_len >= PATH_BUF_SIZE)
            continue; // path too long, skip

        memcpy(full_path + len, cmd, cmd_len);
        full_path[len + cmd_len] = '\0';

        // try to execute
        int pid = try_exec(full_path, argv);
        if (pid >= 0)
            return pid;
    }

    return -1; // not found in any PATH directory
}

void execute(token_list_t *tokens)
{
    const char *cmd = tokens->tokens[0];

    // build a null-terminated argv array from the token list
    const char *argv[TOKEN_MAX + 1];
    for (int i = 0; i < tokens->count; i++)
        argv[i] = tokens->tokens[i];
    argv[tokens->count] = (const char *)0;

    pid_t child;

    if (has_slash(cmd))
    {
        // direct path — execute as-is
        child = exec(cmd, argv);
    }
    else
    {
        // bare command name — search PATH
        child = exec_with_path(cmd, argv);
    }

    if (child < 0)
    {
        printf("%s: command not found\n", cmd);
        shell_last_exit_code = 127;
        return;
    }

    shell_last_exit_code = waitpid(child);
}
