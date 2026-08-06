#include "../../shell/command.h"

#include "../../shell/context.h"

#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"

static int ls_main(int argc, char **argv)
{
    shell_context_t *ctx;
    const char *base_cwd;
    const char *input_path;
    char resolved_path[SHELL_PATH_MAX];

    if (argc > 2)
    {
        printf("ls: too many arguments\r\n");
        return -1;
    }

    ctx = shell_context_get();

    if (argc == 2)
    {
        base_cwd = ctx->cwd;
        input_path = argv[1];
    }
    else
    {
        base_cwd = ctx->cwd;
        input_path = ".";
    }

    if (!vfs_resolve_path(
            base_cwd,
            input_path,
            resolved_path,
            sizeof(resolved_path)))
    {
        printf("ls: invalid path %s\r\n", input_path);
        return -1;
    }

    VFS_FILE dir;

    if (!vfs_open(resolved_path, &dir))
    {
        printf("ls: cannot open %s\r\n", input_path);
        return -1;
    }

    if (vfs_file_type(&dir) != FS_TYPE_DIR)
    {
        printf("ls: %s is not a directory\r\n", input_path);
        vfs_close(&dir);
        return -1;
    }

    FS_DIRENT entry;

    char entry_path[SHELL_PATH_MAX];

    while (vfs_read_entry(&dir, &entry))
    {
        if (!vfs_join_path(
                resolved_path,
                entry.name,
                entry_path,
                sizeof(entry_path)))
        {
            printf("%s\r\n", entry.name);
            continue;
        }

        if (vfs_is_directory(entry_path))
        {
            printf("%s/\r\n", entry.name);
        }
        else
        {
            printf("%s\r\n", entry.name);
        }
    }

    vfs_close(&dir);

    return 0;
}

command_t ls_command =
    {
        .name = "ls",
        .description = "list directory contents",
        .usage = "ls [directory]",
        .main = ls_main,
};

COMMAND_REGISTER(ls_command);