#include "../../shell/command.h"

#include "../../shell/context.h"
#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"
#include "../../../common/string.h"

static int cd_main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: cd <directory>\r\n");
        return 1;
    }

    if (!vfs_is_directory(argv[1]))
    {
        printf("cd: not a directory: %s\r\n", argv[1]);
        return 1;
    }

    shell_context_t *ctx =
        shell_context_get();

    strcpy(ctx->cwd, argv[1]);

    return 0;
}

command_t cd_command =
    {
        .name = "cd",
        .description = "Change current directory",
        .main = cd_main};

COMMAND_REGISTER(cd_command);