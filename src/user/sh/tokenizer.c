/**
 * @file tokenizer.c
 * @brief Tokenizer for shell input.
 *
 * This tokenizer splits a string into tokens based on whitespace, quotes, and escape sequences. It supports:
 * - Double quotes: "hello world" -> one token, spaces preserved
 * - Single quotes: 'hello world' -> one token, fully literal
 * - Backslash escaping: \<space>, \\, \" escape the next character
 * - Quotes mid-token: foo"bar baz"qux -> token "foobar bazqux"
 * - Unquoted whitespace splits tokens
 */

#include "tokenizer.h"

#include <string.h>

// static buffer for token storage (tokens point into this)
static char token_storage[TOKEN_MAX][TOKEN_LEN];

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

        // build a token character by character
        char *out = token_storage[tokens->count];
        int len = 0;

        while (*p && len < TOKEN_LEN - 1)
        {
            if (*p == ' ' || *p == '\t')
            {
                // unquoted whitespace ends the token
                break;
            }
            else if (*p == '\\')
            {
                // backslash escape: take the next character literally
                p++;
                if (*p)
                {
                    out[len++] = *p;
                    p++;
                }
            }
            else if (*p == '\'')
            {
                // single quote: everything until closing quote is literal
                p++; // skip opening quote
                while (*p && *p != '\'')
                {
                    if (len < TOKEN_LEN - 1)
                        out[len++] = *p;
                    p++;
                }
                if (*p == '\'')
                    p++; // skip closing quote
            }
            else if (*p == '"')
            {
                // double quote: everything until closing quote, backslash escapes work
                p++; // skip opening quote
                while (*p && *p != '"')
                {
                    if (*p == '\\' && *(p + 1))
                    {
                        p++;
                        // inside double quotes, only escape \, ", $, newline
                        if (*p == '\\' || *p == '"' || *p == '$')
                        {
                            if (len < TOKEN_LEN - 1)
                                out[len++] = *p;
                            p++;
                        }
                        else
                        {
                            // backslash is literal if not followed by special char
                            if (len < TOKEN_LEN - 1)
                                out[len++] = '\\';
                            if (len < TOKEN_LEN - 1)
                                out[len++] = *p;
                            p++;
                        }
                    }
                    else
                    {
                        if (len < TOKEN_LEN - 1)
                            out[len++] = *p;
                        p++;
                    }
                }
                if (*p == '"')
                    p++; // skip closing quote
            }
            else
            {
                // regular character
                out[len++] = *p;
                p++;
            }
        }

        if (len > 0)
        {
            out[len] = '\0';
            tokens->tokens[tokens->count] = out;
            tokens->count++;
        }
    }
}
