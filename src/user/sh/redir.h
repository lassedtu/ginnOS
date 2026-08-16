// redir.h - redirection and pipe parsing for the shell

#pragma once

#include "sh.h"

#define REDIR_NONE   0
#define REDIR_OUT    1  // >  (truncate)
#define REDIR_APPEND 2  // >> (append)
#define REDIR_IN     3  // <

#define PIPELINE_MAX 8  // maximum stages in a pipeline

/**
 * redirection info extracted from a token list.
 */
typedef struct
{
    int type;                // REDIR_NONE, REDIR_OUT, REDIR_APPEND, or REDIR_IN
    char filename[128];      // target filename
} redir_t;

/**
 * a single pipeline stage (one command with its args and redirections).
 */
typedef struct
{
    token_list_t tokens;     // command + arguments (redirections stripped)
    redir_t redir_in;        // input redirection (< file)
    redir_t redir_out;       // output redirection (> file or >> file)
} pipeline_stage_t;

/**
 * a complete pipeline (cmd1 | cmd2 | cmd3).
 */
typedef struct
{
    pipeline_stage_t stages[PIPELINE_MAX];
    int count;               // number of stages
} pipeline_t;

/**
 * parse a token list into a pipeline (splitting at | operators).
 * also extracts redirections (>, >>, <) from each stage.
 * @param tokens the full token list from the shell input.
 * @param pipeline output pipeline structure.
 */
void pipeline_parse(token_list_t *tokens, pipeline_t *pipeline);
