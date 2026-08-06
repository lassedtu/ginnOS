#include "../../shell/command.h"

#include "../../shell/context.h"
#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"
#include "../../../common/string.h"

static int cd_main(int argc, char **argv)
{
    shell_context_t *ctx;
    char resolved_path[SHELL_PATH_MAX];

    if (argc != 2)
    {
        printf("usage: cd <directory>\r\n");
        return 1;
    }

    ctx = shell_context_get();

    if (!vfs_resolve_path(
            ctx->cwd,
            argv[1],
            resolved_path,
            sizeof(resolved_path)))
    {
        printf("cd: invalid path: %s\r\n", argv[1]);
        return 1;
    }

    if (!vfs_is_directory(resolved_path))
    {
        printf("cd: not a directory: %s\r\n", argv[1]);
        return 1;
    }

    strcpy(ctx->cwd, resolved_path);

    return 0;
}

command_t cd_command =
    {
        .name = "cd",
        .description = "Change current directory",
        .main = cd_main};

COMMAND_REGISTER(cd_command);