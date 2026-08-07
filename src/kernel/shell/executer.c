#include "executor.h"

#include "command.h"

#include "../../common/stdio.h"
#include "../../common/string.h"

void executor_execute(token_list_t *tokens)
{
    if (tokens->count == 0)
    {
        return;
    }

    string_to_lower(tokens->tokens[0]);

    command_t *cmd = command_lookup(tokens->tokens[0]);

    if (!cmd)
    {
        printf(
            "command not found: %s\r\n",
            tokens->tokens[0]);

        return;
    }

    cmd->main(
        tokens->count,
        tokens->tokens);
}