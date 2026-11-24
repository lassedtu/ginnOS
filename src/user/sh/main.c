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
#include "jobs.h"

#define INPUT_MAX 256
#define CWD_MAX 256

static char input_buf[INPUT_MAX];
static char cwd_buf[CWD_MAX];

// global shell state used by expand.c and execute.c
int shell_last_exit_code = 0;

int format_prompt(char *buf, int size)
{
    if (!getcwd(cwd_buf, sizeof(cwd_buf)))
        strcpy(cwd_buf, "?");

    int len = snprintf(buf, size, "skl:%s $ ", cwd_buf);
    if (len >= size)
        len = size - 1; // truncated
    return len;
}

void print_prompt(void)
{
    char prompt[PROMPT_MAX];
    int len = format_prompt(prompt, sizeof(prompt));
    if (len > 0)
        write(1, prompt, len);
}

static int prompt_len(void)
{
    // "skl:" + cwd + " $ "
    int len = 4; // "skl:"
    len += strlen(cwd_buf);
    len += 3; // " $ "
    return len;
}

/**
 * run one command line: variables, then a builtin or a program.
 */
static void run_line(char *line)
{
    token_list_t tokens;
    tokenize(line, &tokens);
    if (tokens.count == 0)
        return;

    // expand variables ($VAR, ${VAR}, $?, $$)
    expand_variables(&tokens);

    // try builtins first, then an external command
    if (!builtin_run(&tokens))
        execute(&tokens);
}

int main(int argc, char **argv)
{
    // "sh -c command": run one line without job control, for system()
    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
    {
        env_init();
        env_set("PATH", "/bin");
        strncpy(input_buf, argv[2], sizeof(input_buf) - 1);
        input_buf[sizeof(input_buf) - 1] = '\0';
        run_line(input_buf);
        return shell_last_exit_code;
    }

    // initialize subsystems
    env_init();
    line_init();
    history_init();
    jobs_init();

    // the environment the shell was started with (TERM from a terminal),
    // then PATH and HOME where it gave none
    for (char **e = environ; e && *e; e++)
    {
        const char *eq = strchr(*e, '=');
        if (!eq || eq == *e || eq - *e >= ENV_KEY_MAX || strlen(eq + 1) >= ENV_VAL_MAX)
            continue;
        char key[ENV_KEY_MAX];
        memcpy(key, *e, (size_t)(eq - *e));
        key[eq - *e] = '\0';
        env_set(key, eq + 1);
    }
    if (!env_get("PATH"))
        env_set("PATH", "/bin");
    if (!env_get("HOME"))
        env_set("HOME", "/");

    while (1)
    {
        // say which background jobs ended or stopped since the last prompt
        jobs_report();

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
        run_line(input_buf);
    }

    return 0;
}
