#include "command.h"

#include "../../common/string.h"

#define MAX_COMMANDS 32

static command_t *commands[MAX_COMMANDS];

static int count = 0;

extern command_t *const __start_shell_cmds[];
extern command_t *const __stop_shell_cmds[];

void command_register(command_t *cmd)
{
    if (count < MAX_COMMANDS)
    {
        commands[count++] = cmd;
    }
}

command_t *command_lookup(const char *name)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(commands[i]->name, name) == 0)
            return commands[i];
    }

    return 0;
}

void commands_initialize(void)
{
    command_t *const *entry;

    count = 0;

    for (entry = __start_shell_cmds; entry < __stop_shell_cmds; entry++)
    {
        if (*entry)
        {
            command_register(*entry);
        }
    }
}
