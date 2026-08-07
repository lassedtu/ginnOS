#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"

/**
 * main function of the cat command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int cat_main(int argc, char **argv)
{
    shell_context_t *ctx;
    char resolved_path[SHELL_PATH_MAX];

    if (argc != 2)
    {
        printf("cat: wrong number of arguments\r\n");
        return -1;
    }

    ctx = shell_context_get();

    if (!vfs_resolve_path(
            ctx->cwd,
            argv[1],
            resolved_path,
            sizeof(resolved_path)))
    {
        printf("cat: invalid path '%s'\r\n", argv[1]);
        return -1;
    }

    VFS_FILE file;

    if (!vfs_open(resolved_path, &file))
    {
        printf("cat: file not found '%s'\r\n", argv[1]);
        return -1;
    }

    char buffer[128];

    uint32_t read;

    while ((read = vfs_read(&file, sizeof(buffer) - 1, buffer)) > 0)
    {
        buffer[read] = 0;
        printf("%s", buffer);
    }

    vfs_close(&file);

    printf("\r\n");

    return 0;
}

static command_t cat_command =
    {
        .name = "cat",
        .description = "Print file contents",
        .usage = "cat <file>",
        .main = cat_main};

COMMAND_REGISTER(cat_command);