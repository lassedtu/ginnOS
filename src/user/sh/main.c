/**
 * @file main.c
 * @brief Main shell program.
 *
 * This file implements the main loop of the shell, handling user input, tokenization, and command execution. It integrates the line editor, built-in commands, and external command execution.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sh.h"
#include "tokenizer.h"
#include "builtin.h"
#include "execute.h"
#include "line.h"

#define INPUT_MAX 256
#define CWD_MAX 256

static char input_buf[INPUT_MAX];
static char cwd_buf[CWD_MAX];

void print_prompt(void)
{
    if (getcwd(cwd_buf, sizeof(cwd_buf)) < 0)
        strcpy(cwd_buf, "?");

    printf("skl:%s $ ", cwd_buf);
}

static int prompt_len(void)
{
    // "skl:" + cwd + " $ "
    int len = 4; // "skl:"
    len += strlen(cwd_buf);
    len += 3; // " $ "
    return len;
}

int main(void)
{
    token_list_t tokens;

    // initialize line editor (switches to raw mode)
    line_init();

    while (1)
    {
        print_prompt();
        line_set_prompt_len(prompt_len());

        int len = line_read(input_buf, sizeof(input_buf));
        if (len < 0)
        {
            // Ctrl+D on empty line — exit
            printf("\n");
            break;
        }
        if (len == 0)
            continue;

        tokenize(input_buf, &tokens);
        if (tokens.count == 0)
            continue;

        // try builtins first
        if (builtin_run(&tokens))
            continue;

        // external command
        execute(&tokens);
    }

    return 0;
}
