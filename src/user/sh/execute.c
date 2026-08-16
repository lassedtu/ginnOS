/**
 * @file execute.c
 * @brief Execute external commands.
 *
 * This file implements the logic for executing external commands in the shell. It handles searching for the command in the system's PATH, preparing the arguments, and invoking the command in a child process.
 */

#include "execute.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PATH_BUF_SIZE 128

void execute(token_list_t *tokens)
{
    const char *cmd = tokens->tokens[0];
    char path[PATH_BUF_SIZE];

    // if the command contains a '/', treat it as a direct path
    if (strchr(cmd, '/'))
    {
        strncpy(path, cmd, PATH_BUF_SIZE - 1);
        path[PATH_BUF_SIZE - 1] = '\0';
    }
    else
    {
        // search in /bin/
        strcpy(path, "/bin/");
        strncpy(path + 5, cmd, PATH_BUF_SIZE - 6);
        path[PATH_BUF_SIZE - 1] = '\0';
    }

    // build a null-terminated argv array from the token list
    // tokens->tokens[] already contains the arguments; we just need
    // to ensure it's null-terminated for the syscall.
    const char *argv[TOKEN_MAX + 1];
    for (int i = 0; i < tokens->count; i++)
        argv[i] = tokens->tokens[i];
    argv[tokens->count] = (const char *)0;

    pid_t child = exec(path, argv);
    if (child < 0)
    {
        printf("%s: command not found\n", cmd);
        return;
    }

    waitpid(child);
}
