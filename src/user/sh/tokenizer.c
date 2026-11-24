/**
 * @file tokenizer.c
 * @brief Tokenizer for shell input.
 *
 * the tokenizer splits a line into words at whitespace, and removes quotes
 * and escapes:
 * - double quotes: "hello world" is one word, with its spaces
 * - single quotes: 'hello world' is one word, fully literal
 * - backslash: \<space>, \\, \" give the next character literally
 * - quotes in a word: foo"bar baz"qux is the word "foobar bazqux"
 * - whitespace that is not quoted ends a word
 */

#include "tokenizer.h"
#include "line.h"

#include <string.h>

// the words of the line, one after the other (tokens point into this). a
// word without its quotes and escapes is never longer than its text, and
// its end byte takes the place of the separator after it, so a line of
// LINE_MAX bytes fits.
static char token_storage[LINE_MAX];

/**
 * copy the double-quoted part at p (after the opening quote) to out.
 * @return where the input continues (after the closing quote).
 */
static char *double_quoted(char *p, char *out, int *len)
{
    while (*p && *p != '"')
    {
        if (*p == '\\' && p[1])
        {
            p++;
            // in double quotes a backslash escapes only \, " and $
            if (*p != '\\' && *p != '"' && *p != '$')
            {
                out[(*len)++] = '\\';
            }
        }
        out[(*len)++] = *p++;
    }
    return *p == '"' ? p + 1 : p;
}

void tokenize(char *input, token_list_t *tokens)
{
    tokens->count = 0;
    if (strlen(input) >= LINE_MAX)
    {
        return; // longer than the line editor and -c allow
    }

    char *p = input;
    char *next = token_storage;
    while (*p && tokens->count < TOKEN_MAX)
    {
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }

        char *out = next;
        int len = 0;
        while (*p && *p != ' ' && *p != '\t')
        {
            if (*p == '\\')
            {
                p++;
                if (*p)
                {
                    out[len++] = *p++;
                }
            }
            else if (*p == '\'')
            {
                p++;
                while (*p && *p != '\'')
                {
                    out[len++] = *p++;
                }
                if (*p == '\'')
                {
                    p++;
                }
            }
            else if (*p == '"')
            {
                p = double_quoted(p + 1, out, &len);
            }
            else
            {
                out[len++] = *p++;
            }
        }

        // an empty word ('' or "") is dropped, as before
        if (len > 0)
        {
            out[len] = '\0';
            tokens->tokens[tokens->count++] = out;
            next = out + len + 1;
        }
    }
}
