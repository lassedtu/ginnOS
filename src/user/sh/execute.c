/**
 * @file execute.c
 * @brief Execute commands and pipelines.
 *
 * This file contains functions to execute commands and pipelines in the shell.
 */

#include "execute.h"
#include "redir.h"
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PATH_BUF_SIZE 256

// external: set by main.c after each command
extern int shell_last_exit_code;

/**
 * check if a path contains a slash.
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
 * search PATH for a command and return the full path.
 * @return 1 if found (result in out_path), 0 if not found.
 */
static int resolve_path(const char *cmd, char *out_path, int out_size)
{
    if (has_slash(cmd))
    {
        strncpy(out_path, cmd, out_size - 1);
        out_path[out_size - 1] = '\0';
        return 1;
    }

    const char *path_var = env_get("PATH");
    if (!path_var || path_var[0] == '\0')
        path_var = "/bin";

    const char *p = path_var;

    while (*p)
    {
        int len = 0;
        while (*p && *p != ':' && len < out_size - 2)
        {
            out_path[len++] = *p;
            p++;
        }
        if (*p == ':')
            p++;

        if (len > 0 && out_path[len - 1] != '/')
            out_path[len++] = '/';

        int cmd_len = strlen(cmd);
        if (len + cmd_len >= out_size)
            continue;

        memcpy(out_path + len, cmd, cmd_len);
        out_path[len + cmd_len] = '\0';

        // try to open to check existence
        int fd = open(out_path, 0);
        if (fd >= 0)
        {
            close(fd);
            return 1;
        }
    }

    return 0;
}

/**
 * apply input/output redirections for the current process.
 * opens files and uses dup2 to redirect stdin/stdout.
 * @return 0 on success, -1 on failure.
 */
static int apply_redirections(redir_t *redir_in, redir_t *redir_out)
{
    if (redir_in->type == REDIR_IN)
    {
        int fd = open(redir_in->filename, 0);
        if (fd < 0)
        {
            printf("skl: cannot open '%s' for input\n", redir_in->filename);
            return -1;
        }
        dup2(fd, 0); // redirect stdin
        close(fd);
    }

    if (redir_out->type == REDIR_OUT || redir_out->type == REDIR_APPEND)
    {
        // create the file if it doesn't exist
        create(redir_out->filename);

        int fd = open(redir_out->filename, 0);
        if (fd < 0)
        {
            printf("skl: cannot open '%s' for output\n", redir_out->filename);
            return -1;
        }

        // for > (truncate mode), truncate the file first
        if (redir_out->type == REDIR_OUT)
        {
            ftruncate(fd);
        }

        // for >> (append mode), seek to end of file
        if (redir_out->type == REDIR_APPEND)
        {
            lseek(fd, 0, SEEK_END);
        }

        dup2(fd, 1); // redirect stdout
        close(fd);
    }

    return 0;
}

/**
 * build the environment for a program from the shell's variables, as
 * "KEY=VALUE" strings. runs in a forked child, so nothing is freed.
 * @return a NULL-terminated array, or NULL if memory ran out.
 */
static char **build_environment(void)
{
    int count = env_count();
    char **envp = malloc((count + 1) * sizeof(char *));
    if (!envp)
        return NULL;

    char key[ENV_KEY_MAX];
    char value[ENV_VAL_MAX];
    int used = 0;
    for (int i = 0; i < count; i++)
    {
        if (env_get_by_index(i, key, value) != 0)
            break;

        int key_len = strlen(key);
        int value_len = strlen(value);
        char *entry = malloc(key_len + value_len + 2);
        if (!entry)
            return NULL;
        memcpy(entry, key, key_len);
        entry[key_len] = '=';
        memcpy(entry + key_len + 1, value, value_len + 1);
        envp[used++] = entry;
    }
    envp[used] = NULL;
    return envp;
}

/**
 * in a forked child: run a stage's command. never returns.
 * @param stage the stage (its argv and redirections).
 * @param path the resolved program path.
 */
static void exec_in_child(pipeline_stage_t *stage, const char *path)
{
    if (apply_redirections(&stage->redir_in, &stage->redir_out) < 0)
        _exit(1);

    char *argv[TOKEN_MAX + 1];
    for (int i = 0; i < stage->tokens.count; i++)
        argv[i] = stage->tokens.tokens[i];
    argv[stage->tokens.count] = NULL;

    execve(path, argv, build_environment());
    printf("skl: %s: cannot execute\n", path);
    _exit(126);
}

/**
 * resolve a stage's command to a program path, printing an error if there
 * is none.
 * @return 1 if found, 0 if not (or the stage is empty).
 */
static int resolve_stage(pipeline_stage_t *stage, char *path)
{
    if (stage->tokens.count == 0)
        return 0;

    const char *cmd = stage->tokens.tokens[0];
    if (!resolve_path(cmd, path, PATH_BUF_SIZE))
    {
        printf("%s: command not found\n", cmd);
        return 0;
    }
    return 1;
}

/**
 * execute a pipeline (one or more stages connected by pipes). every stage
 * runs in a forked child, which sets up its own stdin/stdout and execs the
 * program; the shell's own fds never change.
 */
void execute_pipeline(pipeline_t *pipeline)
{
    if (pipeline->count == 0)
        return;

    pid_t children[PIPELINE_MAX];
    int pipe_fds[PIPELINE_MAX - 1][2]; // pipe_fds[i] connects stage i to stage i+1
    int pipe_count = pipeline->count - 1;

    for (int i = 0; i < pipe_count; i++)
    {
        if (pipe(pipe_fds[i]) < 0)
        {
            printf("skl: failed to create pipe\n");
            for (int j = 0; j < i; j++)
            {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }
            shell_last_exit_code = 1;
            return;
        }
    }

    for (int i = 0; i < pipeline->count; i++)
    {
        pipeline_stage_t *stage = &pipeline->stages[i];
        char path[PATH_BUF_SIZE];

        children[i] = -1;
        if (!resolve_stage(stage, path))
            continue;

        pid_t child = fork();
        if (child < 0)
        {
            printf("skl: fork failed\n");
            continue;
        }

        if (child == 0)
        {
            // stdin from the previous stage, stdout to the next one; an
            // explicit redirection in the stage overrides these
            if (i > 0)
                dup2(pipe_fds[i - 1][0], 0);
            if (i < pipe_count)
                dup2(pipe_fds[i][1], 1);

            // the child keeps only its stdio ends, so a reader sees EOF
            // once every writer has exited
            for (int j = 0; j < pipe_count; j++)
            {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }

            exec_in_child(stage, path);
        }

        children[i] = child;
    }

    for (int i = 0; i < pipe_count; i++)
    {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }

    // the status of a pipeline is that of its last stage: its exit code,
    // or 128 + the signal that killed it
    int last_exit = 127;
    for (int i = 0; i < pipeline->count; i++)
    {
        int status;
        if (children[i] < 0 || waitpid(children[i], &status, 0) < 0)
            last_exit = 127;
        else if (WIFEXITED(status))
            last_exit = WEXITSTATUS(status);
        else
            last_exit = 128 + WTERMSIG(status);
    }

    shell_last_exit_code = last_exit;
}

// keep the old execute() as a convenience wrapper for main.c
void execute(token_list_t *tokens)
{
    pipeline_t pipeline;
    pipeline_parse(tokens, &pipeline);
    execute_pipeline(&pipeline);
}
