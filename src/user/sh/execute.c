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
 * restore stdin/stdout to console after redirection.
 * uses dup2 from stderr (fd 2, never redirected) to reset 0 and 1.
 * Note: for pipe fds, this will decrement ref counts, but the child
 * already has its own reference so the pipe stays alive.
 */
static void restore_stdio(void)
{
    dup2(2, 0);
    dup2(2, 1);
}

/**
 * execute a single stage (one command with optional redirections).
 * used for both simple commands and individual pipeline stages.
 */
static pid_t exec_stage(pipeline_stage_t *stage)
{
    if (stage->tokens.count == 0)
        return -1;

    const char *cmd = stage->tokens.tokens[0];
    char path[PATH_BUF_SIZE];

    if (!resolve_path(cmd, path, sizeof(path)))
    {
        printf("%s: command not found\n", cmd);
        return -1;
    }

    // build argv
    const char *argv[TOKEN_MAX + 1];
    for (int i = 0; i < stage->tokens.count; i++)
        argv[i] = stage->tokens.tokens[i];
    argv[stage->tokens.count] = (const char *)0;

    pid_t child = exec(path, argv);
    return child;
}

/**
 * execute a pipeline (one or more stages connected by pipes).
 */
void execute_pipeline(pipeline_t *pipeline)
{
    if (pipeline->count == 0)
        return;

    // single command. simple case with redirection
    if (pipeline->count == 1)
    {
        pipeline_stage_t *stage = &pipeline->stages[0];

        int has_redir = (stage->redir_in.type != REDIR_NONE ||
                         stage->redir_out.type != REDIR_NONE);

        if (has_redir)
        {
            if (apply_redirections(&stage->redir_in, &stage->redir_out) < 0)
            {
                shell_last_exit_code = 1;
                restore_stdio();
                return;
            }
        }

        pid_t child = exec_stage(stage);

        if (has_redir)
            restore_stdio();

        if (child < 0)
        {
            shell_last_exit_code = 127;
            return;
        }

        shell_last_exit_code = waitpid(child);
        return;
    }

    // multi-stage pipeline
    pid_t children[PIPELINE_MAX];
    int pipe_fds[PIPELINE_MAX - 1][2]; // pipe_fds[i] connects stage i → stage i+1

    // create all pipes
    for (int i = 0; i < pipeline->count - 1; i++)
    {
        if (pipe(pipe_fds[i]) < 0)
        {
            printf("skl: failed to create pipe\n");
            shell_last_exit_code = 1;
            return;
        }
    }

    // spawn each stage
    for (int i = 0; i < pipeline->count; i++)
    {
        pipeline_stage_t *stage = &pipeline->stages[i];

        // set up redirections for this stage
        // stdin: from previous pipe's read end (except first stage)
        if (i > 0)
            dup2(pipe_fds[i - 1][0], 0);

        // stdout: to next pipe's write end (except last stage)
        if (i < pipeline->count - 1)
            dup2(pipe_fds[i][1], 1);

        // apply explicit redirections (override pipe if specified)
        if (stage->redir_in.type != REDIR_NONE)
        {
            int fd = open(stage->redir_in.filename, 0);
            if (fd >= 0)
            {
                dup2(fd, 0);
                close(fd);
            }
        }
        if (stage->redir_out.type != REDIR_NONE)
        {
            create(stage->redir_out.filename);
            int fd = open(stage->redir_out.filename, 0);
            if (fd >= 0)
            {
                dup2(fd, 1);
                close(fd);
            }
        }

        children[i] = exec_stage(stage);

        // restore stdin/stdout for the shell (so next iteration works)
        restore_stdio();
    }

    // close all pipe fds in the shell (children already have what they need)
    for (int i = 0; i < pipeline->count - 1; i++)
    {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }

    // wait for all children
    int last_exit = 0;
    for (int i = 0; i < pipeline->count; i++)
    {
        if (children[i] >= 0)
            last_exit = waitpid(children[i]);
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
