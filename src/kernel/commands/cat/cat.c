#include "../../shell/command.h"

#include "../../vfs/vfs.h"
#include "../../../common/stdio.h"

/**
 * main function of the cat command.
 * @param argc number of arguments.
 * @param argv array of argument strings.
 * @return return code of the command.
 */
static int cat_execute(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: cat <file>\r\n");
        return -1;
    }

    VFS_FILE file;

    if (!vfs_open(argv[1], &file))
    {
        printf("cat: cannot open '%s'\r\n", argv[1]);
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
        .main = cat_execute};

COMMAND_REGISTER(cat_command);