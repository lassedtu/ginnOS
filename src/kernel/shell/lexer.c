#include "lexer.h"

void lexer_tokenize(
    char *input,
    token_list_t *tokens)
{
    char *p = input;

    tokens->count = 0;

    while (*p)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }

        if (!*p)
            break;

        if (*p == '"')
        {
            p++;

            tokens->tokens[tokens->count++] = p;

            while (*p && *p != '"')
            {
                p++;
            }

            if (*p == '"')
            {
                *p = 0;
                p++;
            }
        }
        else
        {
            tokens->tokens[tokens->count++] = p;

            while (*p &&
                   *p != ' ' &&
                   *p != '\t')
            {
                p++;
            }

            if (*p)
            {
                *p = 0;
                p++;
            }
        }
    }
}