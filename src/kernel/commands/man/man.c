#include "../../shell/command.h"

#include "../../../common/stdio.h"

/**
 * main function of the man command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int man_main(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        printf("Usage: man <command>\r\n");
        return -1;
    }

    command_t *cmd =
        command_lookup(argv[1]);

    if (!cmd)
    {
        printf("No manual entry for %s\r\n",
               argv[1]);

        return -1;
    }

    printf("%s\r\n", cmd->name);
    printf("  %s\r\n",
           cmd->description);

    if (cmd->usage)
    {
        printf("  Usage: %s\r\n",
               cmd->usage);
    }

    return 0;
}

static command_t man_command =
    {
        .name = "man",
        .description = "Display command information",
        .usage = "man <command>",
        .main = man_main};

COMMAND_REGISTER(man_command);