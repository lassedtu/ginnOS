/**
 * @file builtin.c
 * @brief Built-in shell commands.
 *
 * This file implements the built-in commands for the shell, such as `cd` and `exit`. Built-in commands are executed directly by the shell without invoking external programs.
 */

#include "builtin.h"
#include "history.h"
#include "env.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// forward declarations for builtin table (used by 'which')
typedef struct
{
    const char *name;
    int (*fn)(token_list_t *tokens);
} builtin_entry_t;

static builtin_entry_t builtins[];

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

static int builtin_history(token_list_t *tokens)
{
    (void)tokens;
    int count = history_count();

    for (int i = 0; i < count; i++)
    {
        const char *entry = history_get(i);
        if (entry)
            printf(" %d  %s\n", i + 1, entry);
    }

    return 1;
}

static int builtin_export(token_list_t *tokens)
{
    if (tokens->count < 2)
    {
        // no args: print all variables (same as env)
        char key[ENV_KEY_MAX];
        char val[ENV_VAL_MAX];
        int count = env_count();

        for (int i = 0; i < count; i++)
        {
            if (env_get_by_index(i, key, val) == 0)
                printf("export %s=\"%s\"\n", key, val);
        }
        return 1;
    }

    // parse each argument as KEY=VALUE or KEY
    for (int i = 1; i < tokens->count; i++)
    {
        char *arg = tokens->tokens[i];
        char *eq = strchr(arg, '=');

        if (eq)
        {
            // KEY=VALUE
            *eq = '\0';
            env_set(arg, eq + 1);
            *eq = '='; // restore in case token is reused
        }
        else
        {
            // KEY with no value — set to empty string
            env_set(arg, "");
        }
    }

    return 1;
}

static int builtin_unset(token_list_t *tokens)
{
    if (tokens->count < 2)
    {
        printf("unset: usage: unset <name> [name...]\n");
        return 1;
    }

    for (int i = 1; i < tokens->count; i++)
        env_unset(tokens->tokens[i]);

    return 1;
}

static int builtin_env(token_list_t *tokens)
{
    (void)tokens;
    char key[ENV_KEY_MAX];
    char val[ENV_VAL_MAX];
    int count = env_count();

    for (int i = 0; i < count; i++)
    {
        if (env_get_by_index(i, key, val) == 0)
            printf("%s=%s\n", key, val);
    }

    return 1;
}

static int builtin_printenv(token_list_t *tokens)
{
    if (tokens->count < 2)
    {
        // no args: same as env
        char key[ENV_KEY_MAX];
        char val[ENV_VAL_MAX];
        int count = env_count();

        for (int i = 0; i < count; i++)
        {
            if (env_get_by_index(i, key, val) == 0)
                printf("%s=%s\n", key, val);
        }
    }
    else
    {
        // print specific variables
        for (int i = 1; i < tokens->count; i++)
        {
            const char *val = env_get(tokens->tokens[i]);
            if (val)
                printf("%s\n", val);
        }
    }

    return 1;
}

static int builtin_which(token_list_t *tokens)
{
    if (tokens->count < 2)
    {
        printf("which: usage: which <command> [command...]\n");
        return 1;
    }

    const char *path_var = env_get("PATH");
    if (!path_var || path_var[0] == '\0')
        path_var = "/bin";

    for (int i = 1; i < tokens->count; i++)
    {
        const char *cmd = tokens->tokens[i];
        int found = 0;

        // check if it's a builtin
        for (int b = 0; builtins[b].name; b++)
        {
            if (strcmp(cmd, builtins[b].name) == 0)
            {
                printf("%s: shell built-in command\n", cmd);
                found = 1;
                break;
            }
        }

        if (found)
            continue;

        // search PATH
        const char *p = path_var;
        char full_path[256];

        while (*p)
        {
            int len = 0;
            while (*p && *p != ':' && len < 250)
            {
                full_path[len++] = *p;
                p++;
            }
            if (*p == ':')
                p++;

            if (len > 0 && full_path[len - 1] != '/')
                full_path[len++] = '/';

            int cmd_len = strlen(cmd);
            if (len + cmd_len >= 256)
                continue;

            memcpy(full_path + len, cmd, cmd_len);
            full_path[len + cmd_len] = '\0';

            // try to open the file to check if it exists
            int fd = open(full_path, 0);
            if (fd >= 0)
            {
                close(fd);
                printf("%s\n", full_path);
                found = 1;
                break;
            }
        }

        if (!found)
            printf("%s not found\n", cmd);
    }

    return 1;
}

// table of built-in commands
static builtin_entry_t builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"history", builtin_history},
    {"export", builtin_export},
    {"unset", builtin_unset},
    {"env", builtin_env},
    {"printenv", builtin_printenv},
    {"which", builtin_which},
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
