/**
 * @file expand.c
 * @brief Variable expansion for shell tokens.
 *
 * This file implements functions to expand environment variables and special shell variables (like $? and $$) in shell tokens. It processes tokens containing variable references and replaces them with their corresponding values.
 */

#include "expand.h"
#include "env.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

// external state from shell main
extern int shell_last_exit_code;

// buffer for expanded tokens
static char expand_buf[TOKEN_MAX][TOKEN_LEN];

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
    for (int i = 0; i < tokens->count; i++)
    {
        // only expand if the token contains a $
        if (strchr(tokens->tokens[i], '$'))
        {
            expand_token(tokens->tokens[i], expand_buf[i], TOKEN_LEN);
            tokens->tokens[i] = expand_buf[i];
        }
    }
}
