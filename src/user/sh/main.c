// sh - ginnOS userspace shell (skl)

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sh.h"
#include "tokenizer.h"
#include "builtin.h"
#include "execute.h"
#include "expand.h"
#include "line.h"
#include "history.h"
#include "env.h"

#define INPUT_MAX 256
#define CWD_MAX 256

static char input_buf[INPUT_MAX];
static char cwd_buf[CWD_MAX];

// global shell state used by expand.c and execute.c
int shell_last_exit_code = 0;

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

    // initialize subsystems
    env_init();
    line_init();
    history_init();

    // set default environment
    env_set("PATH", "/bin");
    env_set("HOME", "/");

    while (1)
    {
        print_prompt();
        line_set_prompt_len(prompt_len());

        int len = line_read(input_buf, sizeof(input_buf));
        if (len < 0)
        {
            // Ctrl+D on empty line: exit
            printf("\n");
            break;
        }
        if (len == 0)
            continue;

        // add to history before executing
        history_add(input_buf);

        tokenize(input_buf, &tokens);
        if (tokens.count == 0)
            continue;

        // expand variables ($VAR, ${VAR}, $?, $$)
        expand_variables(&tokens);

        // try builtins first
        if (builtin_run(&tokens))
            continue;

        // external command
        execute(&tokens);
    }

    return 0;
}
