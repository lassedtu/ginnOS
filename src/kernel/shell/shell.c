#include "shell.h"

#include "context.h"
#include "lexer.h"
#include "executor.h"

#include "../console/console.h"
#include "../../common/stdio.h"

static char *shell_read_line(char *buffer, int size)
{
    if (console_readline(buffer, size) < 0)
    {
        buffer[0] = 0;
    }

    return buffer;
}

void shell_initialize(void)
{
    printf("shell (skl): initialized\r\n");
}

void shell_run(void)
{
    char buffer[128];

    while (1)
    {
        shell_context_t *ctx = shell_context_get();

        printf("skl:%s $ ", ctx->cwd);

        shell_read_line(buffer, sizeof(buffer));

        token_list_t tokens;

        lexer_tokenize(buffer, &tokens);

        executor_execute(&tokens);
    }
}