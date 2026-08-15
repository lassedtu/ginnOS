// sh - ginnOS userspace shell

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sh.h"
#include "tokenizer.h"
#include "builtin.h"
#include "execute.h"

#define INPUT_MAX 256
#define CWD_MAX   256

static char input_buf[INPUT_MAX];
static char cwd_buf[CWD_MAX];

// read a line from stdin.
// the kernel handles echo, backspace, and line buffering.
// returns number of characters read (excluding null terminator).
static int read_line(char *buf, int size)
{
    int n = read(0, buf, size - 1);
    if (n <= 0)
    {
        buf[0] = '\0';
        return 0;
    }

    // strip trailing newline if present
    if (n > 0 && buf[n - 1] == '\n')
        n--;

    buf[n] = '\0';
    return n;
}

static void print_prompt(void)
{
    if (getcwd(cwd_buf, sizeof(cwd_buf)) < 0)
        strcpy(cwd_buf, "?");

    printf("skl:%s $ ", cwd_buf);
}

int main(void)
{
    token_list_t tokens;

    while (1)
    {
        print_prompt();

        int len = read_line(input_buf, sizeof(input_buf));
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
