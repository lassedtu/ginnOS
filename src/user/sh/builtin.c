/**
 * @file builtin.c
 * @brief Built-in shell commands.
 *
 * This file implements the built-in commands for the shell, such as `cd` and `exit`. Built-in commands are executed directly by the shell without invoking external programs.
 */

#include "builtin.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int builtin_cd(token_list_t *tokens)
{
    const char *path;

    if (tokens->count < 2)
        path = "/";
    else
        path = tokens->tokens[1];

    if (chdir(path) < 0)
        printf("cd: %s: no such directory\n", path);

    return 1;
}

static int builtin_exit(token_list_t *tokens)
{
    int code = 0;

    if (tokens->count >= 2)
        code = atoi(tokens->tokens[1]);

    exit(code);
    return 1; // unreachable
}

// table of built-in commands
typedef struct
{
    const char *name;
    int (*fn)(token_list_t *tokens);
} builtin_entry_t;

static builtin_entry_t builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {0, 0},
};

int builtin_run(token_list_t *tokens)
{
    const char *cmd = tokens->tokens[0];

    for (int i = 0; builtins[i].name; i++)
    {
        if (strcmp(cmd, builtins[i].name) == 0)
            return builtins[i].fn(tokens);
    }

    return 0;
}
