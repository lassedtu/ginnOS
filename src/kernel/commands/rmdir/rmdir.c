#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"

#include "../../../common/stdio.h"

static int rmdir_directory(const char *input_path);

/**
 * main function of the rmdir command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int rmdir_main(int argc, char **argv)
{
    int result = 0;

    if (argc < 2)
    {
        printf("Usage: rmdir <directory>...\r\n");
        return -1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (rmdir_directory(argv[i]) != 0)
        {
            result = -1;
        }
    }

    return result;
}

static int rmdir_directory(const char *input_path)
{
    shell_context_t *ctx;
    char path[SHELL_PATH_MAX];
    VFS_STAT stat;

    ctx = shell_context_get();

    if (!vfs_resolve_path(
            ctx->cwd,
            input_path,
            path,
            sizeof(path)))
    {
        printf("rmdir: invalid path %s\r\n", input_path);
        return -1;
    }

    VFS_STATUS stat_status = vfs_stat(path, &stat);
    if (stat_status != VFS_OK)
    {
        printf("rmdir: %s does not exist\r\n", input_path);
        return -1;
    }

    if (stat.file_type != FS_TYPE_DIR)
    {
        printf("rmdir: %s is not a directory\r\n", input_path);
        return -1;
    }

    if (!vfs_rmdir(path))
    {
        printf("rmdir: failed to remove %s\r\n", input_path);
        return -1;
    }

    return 0;
}

command_t rmdir_command =
    {
        .name = "rmdir",
        .description = "Remove a directory",
        .usage = "rmdir <directory>...",
        .main = rmdir_main,
};

COMMAND_REGISTER(rmdir_command);