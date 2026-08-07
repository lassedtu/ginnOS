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
        printf("cd: wrong number of arguments\r\n");
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

    VFS_FILE dir;

    if (!vfs_open(resolved_path, &dir))
    {
        printf("cd: cannot open: %s\r\n", argv[1]);
        return 1;
    }

    if (vfs_file_type(&dir) != FS_TYPE_DIR)
    {
        printf("cd: not a directory: %s\r\n", argv[1]);
        vfs_close(&dir);
        return 1;
    }

    vfs_close(&dir);

    strcpy(ctx->cwd, resolved_path);

    return 0;
}

command_t cd_command =
    {
        .name = "cd",
        .description = "Change current directory",
        .main = cd_main};

COMMAND_REGISTER(cd_command);