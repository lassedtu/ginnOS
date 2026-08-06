#include "../../shell/command.h"

#include "../../../common/stdio.h"

/**
 * main function of the echo command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int echo_main(
    int argc,
    char **argv)
{
    if (argc < 2)
    {
        printf("Usage: echo <text>\r\n");
        return -1;
    }

    for (int i = 1; i < argc; i++)
    {
        printf("%s", argv[i]);

        if (i + 1 < argc)
            printf(" ");
    }

    printf("\r\n");

    return 0;
}

static command_t echo_command =
    {
        .name = "echo",
        .description = "Write arguments to the standard output",
        .usage = "echo <text ...>",
        .main = echo_main};

COMMAND_REGISTER(echo_command);