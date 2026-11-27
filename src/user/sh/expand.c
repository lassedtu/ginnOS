/**
 * @file expand.c
 * @brief Variable expansion for shell tokens.
 *
 * the words of a command get their variables replaced: $NAME and ${NAME}
 * (the environment), $? (the last exit status) and $$ (the shell's pid).
 * it runs after the quotes are removed, so $ in single quotes is
 * replaced too (a known gap).
 */

#include "expand.h"
#include "env.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

// external state from shell main
extern int shell_last_exit_code;

// the expanded tokens, one after the other
#define EXPAND_MAX 16384
static char expand_buf[EXPAND_MAX];

/**
 * check if a character is valid in a variable name.
 */
static int is_var_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

/**
 * simple integer to string.
 */
static void int_to_str(int val, char *buf, int bufsize)
{
    int neg = 0;
    char tmp[16];
    int i = 0;

    if (val < 0)
    {
        neg = 1;
        val = -val;
    }

    do
    {
        tmp[i++] = (char)('0' + val % 10);
        val /= 10;
    } while (val > 0 && i < 15);

    int out = 0;
    if (neg && out < bufsize - 1)
        buf[out++] = '-';

    while (i > 0 && out < bufsize - 1)
        buf[out++] = tmp[--i];

    buf[out] = '\0';
}

/**
 * expand a single token, writing result into out_buf.
 * @return length of expanded string.
 */
static int expand_token(const char *in, char *out, int out_size)
{
    int oi = 0;

    while (*in && oi < out_size - 1)
    {
        if (*in == '$')
        {
            in++;

            if (*in == '?')
            {
                // $?: last exit code
                in++;
                char tmp[16];
                int_to_str(shell_last_exit_code, tmp, sizeof(tmp));
                for (int j = 0; tmp[j] && oi < out_size - 1; j++)
                    out[oi++] = tmp[j];
            }
            else if (*in == '$')
            {
                // $$: shell PID
                in++;
                char tmp[16];
                int_to_str(getpid(), tmp, sizeof(tmp));
                for (int j = 0; tmp[j] && oi < out_size - 1; j++)
                    out[oi++] = tmp[j];
            }
            else if (*in == '{')
            {
                // ${VAR}: braced variable
                in++; // skip {
                char varname[ENV_KEY_MAX];
                int vi = 0;
                while (*in && *in != '}' && vi < ENV_KEY_MAX - 1)
                    varname[vi++] = *in++;
                varname[vi] = '\0';
                if (*in == '}')
                    in++; // skip }

                const char *val = env_get(varname);
                if (val)
                {
                    for (int j = 0; val[j] && oi < out_size - 1; j++)
                        out[oi++] = val[j];
                }
            }
            else if (is_var_char(*in))
            {
                // $VAR: unbraced variable
                char varname[ENV_KEY_MAX];
                int vi = 0;
                while (is_var_char(*in) && vi < ENV_KEY_MAX - 1)
                    varname[vi++] = *in++;
                varname[vi] = '\0';

                const char *val = env_get(varname);
                if (val)
                {
                    for (int j = 0; val[j] && oi < out_size - 1; j++)
                        out[oi++] = val[j];
                }
            }
            else
            {
                // lone $: output literally
                out[oi++] = '$';
            }
        }
        else
        {
            out[oi++] = *in++;
        }
    }

    out[oi] = '\0';
    return oi;
}

void expand_variables(token_list_t *tokens)
{
    int used = 0;
    for (int i = 0; i < tokens->count; i++)
    {
        // only expand if the token contains a $ (and while there is room)
        if (strchr(tokens->tokens[i], '$') && used < EXPAND_MAX - 1)
        {
            char *out = expand_buf + used;
            used += expand_token(tokens->tokens[i], out, EXPAND_MAX - used) + 1;
            tokens->tokens[i] = out;
        }
    }
}
