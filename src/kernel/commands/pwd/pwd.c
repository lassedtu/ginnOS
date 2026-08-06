#include "../../shell/command.h"
#include "../../shell/context.h"

#include "../../../common/stdio.h"

/**
 * main function of the pwd command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int pwd_main(
    int argc,
    char **argv)
{
    if (argc != 1)
    {
        // Process IDs not yet implemented
        printf("pwd: too many arguments\r\n");
        return -1;
    }

    shell_context_t *ctx = shell_context_get();
    printf("%s%s\n", ctx->cwd, ctx->cwd[1] == '\0' ? "" : "/");

    return 0;
}

command_t pwd_command =
    {
        .name = "pwd",
        .description = "print working directory",
        .usage = "pwd",
        .main = pwd_main,
};

COMMAND_REGISTER(pwd_command);