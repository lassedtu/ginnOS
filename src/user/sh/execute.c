// execute.c - external command execution via exec + waitpid

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

    pid_t child = exec(path);
    if (child < 0)
    {
        printf("%s: command not found\n", cmd);
        return;
    }

    waitpid(child);
}
