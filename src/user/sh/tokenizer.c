/**
 * @file tokenizer.c
 * @brief Tokenizer for shell input.
 *
 * This file implements a simple tokenizer for shell input. It splits a line of input into tokens based on whitespace, preparing them for command execution.
 */

#include "tokenizer.h"

void tokenize(char *input, token_list_t *tokens)
{
    tokens->count = 0;

    char *p = input;

    while (*p && tokens->count < TOKEN_MAX)
    {
        // skip leading whitespace
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        // mark start of token
        tokens->tokens[tokens->count++] = p;

        // find end of token
        while (*p && *p != ' ' && *p != '\t')
            p++;

        // null-terminate the token
        if (*p)
            *p++ = '\0';
    }
}
