#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"

#include "../../../common/stdio.h"

static int mkdir_directory(const char *input_path);

/**
 * main function of the mkdir command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int mkdir_main(int argc, char **argv)
{
    int result = 0;

    if (argc < 2)
    {
        printf("Usage: mkdir <directory>...\r\n");
        return -1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (mkdir_directory(argv[i]) != 0)
        {
            result = -1;
        }
    }

    return result;
}

static int mkdir_directory(const char *input_path)
{
    shell_context_t *ctx;
    char path[SHELL_PATH_MAX];

    ctx = shell_context_get();

    if (!vfs_resolve_path(
            ctx->cwd,
            input_path,
            path,
            sizeof(path)))
    {
        printf("mkdir: invalid path %s\r\n", input_path);
        return -1;
    }

    if (!vfs_mkdir(path))
    {
        printf("mkdir: failed to create %s\r\n", input_path);
        return -1;
    }

    return 0;
}

command_t mkdir_command =
    {
        .name = "mkdir",
        .description = "Create a directory",
        .usage = "mkdir <directory>...",
        .main = mkdir_main,
};

COMMAND_REGISTER(mkdir_command);