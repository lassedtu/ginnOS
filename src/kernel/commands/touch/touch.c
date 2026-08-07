#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"

#include "../../../common/stdio.h"

static int touch_create_file(const char *input_path);

/**
 * main function of the touch command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int touch_main(int argc, char **argv)
{
    int result = 0;

    if (argc < 2)
    {
        printf("Usage: touch <file>...\r\n");
        return -1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (touch_create_file(argv[i]) != 0)
        {
            result = -1;
        }
    }

    return result;
}

static int touch_create_file(const char *input_path)
{
    shell_context_t *ctx;
    VFS_STATUS stat_status;
    VFS_STAT stat;
    char path[SHELL_PATH_MAX];

    ctx = shell_context_get();

    if (!vfs_resolve_path(
            ctx->cwd,
            input_path,
            path,
            sizeof(path)))
    {
        printf("touch: invalid path\r\n");
        return -1;
    }

    stat_status = vfs_stat(path, &stat);
    if (stat_status == VFS_OK)
    {
        if (stat.file_type == FS_TYPE_DIR)
        {
            printf("touch: %s is a directory\r\n", input_path);
            return -1;
        }

        /*
         * file already exists.
         *
         * timestamp updating is not implemented yet,
         * so just return success for now.
         */
        return 0;
    }

    if (stat_status == VFS_IO_ERROR)
    {
        printf("touch: cannot access %s: I/O error\r\n", input_path);
        return -1;
    }

    if (stat_status == VFS_PERMISSION_DENIED)
    {
        printf("touch: cannot access %s: permission denied\r\n", input_path);
        return -1;
    }

    if (!vfs_create(path))
    {
        printf("touch: failed to create %s\r\n", input_path);
        return -1;
    }

    return 0;
}

command_t touch_command =
    {
        .name = "touch",
        .description = "Create one or more empty files",
        .usage = "touch <file>...",
        .main = touch_main,
};

COMMAND_REGISTER(touch_command);