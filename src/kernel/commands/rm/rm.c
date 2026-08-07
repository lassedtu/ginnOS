#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"

#include "../../../common/stdio.h"

static int remove_file(const char *input_path);

/**
 * main function of the rm command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int rm_main(int argc, char **argv)
{
    int result = 0;

    if (argc < 2)
    {
        printf("Usage: rm <file>...\r\n");
        return -1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (remove_file(argv[i]) != 0)
        {
            result = -1;
        }
    }

    return result;
}

static int remove_file(const char *input_path)
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
        printf("rm: invalid path %s\r\n", input_path);
        return -1;
    }

    VFS_STATUS stat_status = vfs_stat(path, &stat);
    if (stat_status != VFS_OK)
    {
        printf("rm: cannot remove %s: no such file\r\n", input_path);
        return -1;
    }

    if (stat.file_type == FS_TYPE_DIR)
    {
        printf("rm: %s is a directory\r\n", input_path);
        return -1;
    }

    if (!vfs_remove(path))
    {
        printf("rm: failed to remove %s\r\n", input_path);
        return -1;
    }

    return 0;
}

command_t rm_command =
    {
        .name = "rm",
        .description = "Remove a file",
        .usage = "rm <file>...",
        .main = rm_main,
};

COMMAND_REGISTER(rm_command);