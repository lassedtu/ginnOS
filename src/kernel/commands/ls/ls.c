#include "../../shell/command.h"

#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"

static int ls_main(int argc, char **argv)
{
    const char *path = "/";

    if (argc > 1)
    {
        printf("Usage: ls [directory]\r\n");
        return -1;
    }

    VFS_FILE dir;

    if (!vfs_open(path, &dir))
    {
        printf("ls: cannot open %s\r\n", path);
        return -1;
    }

    if (vfs_file_type(&dir) != FS_TYPE_DIR)
    {
        printf("ls: %s is not a directory\r\n", path);
        vfs_close(&dir);
        return -1;
    }

    FS_DIRENT entry;

    while (vfs_read_entry(&dir, &entry))
    {
        printf("%s\r\n", entry.name);
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