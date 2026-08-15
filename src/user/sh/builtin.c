// builtin.c - shell built-in commands (cd, exit, echo)

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

static int builtin_echo(token_list_t *tokens)
{
    for (int i = 1; i < tokens->count; i++)
    {
        if (i > 1)
            putchar(' ');
        printf("%s", tokens->tokens[i]);
    }
    putchar('\n');
    return 1;
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
    {"echo", builtin_echo},
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
