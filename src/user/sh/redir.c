// redir.c - redirection and pipe parsing

#include "redir.h"

#include <string.h>

/**
 * check if a token is a redirection operator.
 * @return REDIR_OUT, REDIR_APPEND, REDIR_IN, or REDIR_NONE.
 */
static int redir_type(const char *tok)
{
    if (tok[0] == '>' && tok[1] == '>')
        return REDIR_APPEND;
    if (tok[0] == '>' && tok[1] == '\0')
        return REDIR_OUT;
    if (tok[0] == '<' && tok[1] == '\0')
        return REDIR_IN;
    return REDIR_NONE;
}

/**
 * extract redirections from a stage's tokens.
 * removes redirection operators and filenames from the token list.
 */
static void extract_redirections(pipeline_stage_t *stage)
{
    token_list_t *t = &stage->tokens;
    token_list_t cleaned;
    cleaned.count = 0;

    stage->redir_in.type = REDIR_NONE;
    stage->redir_out.type = REDIR_NONE;

    int i = 0;
    while (i < t->count)
    {
        int rtype = redir_type(t->tokens[i]);

        if (rtype != REDIR_NONE)
        {
            // next token is the filename
            if (i + 1 < t->count)
            {
                if (rtype == REDIR_IN)
                {
                    stage->redir_in.type = rtype;
                    strncpy(stage->redir_in.filename, t->tokens[i + 1], 127);
                    stage->redir_in.filename[127] = '\0';
                }
                else
                {
                    stage->redir_out.type = rtype;
                    strncpy(stage->redir_out.filename, t->tokens[i + 1], 127);
                    stage->redir_out.filename[127] = '\0';
                }
                i += 2; // skip operator + filename
            }
            else
            {
                // missing filename after operator: skip
                i++;
            }
        }
        else
        {
            // regular token: keep it
            if (cleaned.count < TOKEN_MAX)
                cleaned.tokens[cleaned.count++] = t->tokens[i];
            i++;
        }
    }

    *t = cleaned;
}

void pipeline_parse(token_list_t *tokens, pipeline_t *pipeline)
{
    pipeline->count = 0;

    if (tokens->count == 0)
        return;

    // split tokens at | operators into stages
    int stage_start = 0;

    for (int i = 0; i <= tokens->count; i++)
    {
        int is_pipe = (i < tokens->count && tokens->tokens[i][0] == '|' && tokens->tokens[i][1] == '\0');
        int is_end = (i == tokens->count);

        if (is_pipe || is_end)
        {
            if (pipeline->count >= PIPELINE_MAX)
                break;

            pipeline_stage_t *stage = &pipeline->stages[pipeline->count];
            stage->tokens.count = 0;
            stage->redir_in.type = REDIR_NONE;
            stage->redir_out.type = REDIR_NONE;

            // copy tokens for this stage
            for (int j = stage_start; j < i; j++)
            {
                if (stage->tokens.count < TOKEN_MAX)
                    stage->tokens.tokens[stage->tokens.count++] = tokens->tokens[j];
            }

            // extract redirections from this stage
            extract_redirections(stage);

            pipeline->count++;
            stage_start = i + 1; // skip the | token
        }
    }
}
