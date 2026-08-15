#include "fd_table.h"
#include "../../common/memory.h"

static fd_entry_t fd_table[FD_MAX];

void fd_table_init(void)
{
    memset(fd_table, 0, sizeof(fd_table));

    /* pre-open stdin, stdout, stderr as console descriptors */
    fd_table[0].type = FD_TYPE_CONSOLE;
    fd_table[1].type = FD_TYPE_CONSOLE;
    fd_table[2].type = FD_TYPE_CONSOLE;
}

int fd_alloc(VFS_FILE *file)
{
    /* start searching at fd 3 (0/1/2 are reserved) */
    for (int i = 3; i < FD_MAX; i++)
    {
        if (fd_table[i].type == FD_TYPE_NONE)
        {
            fd_table[i].type = FD_TYPE_FILE;
            fd_table[i].file = *file;
            return i;
        }
    }

    return -1;
}

fd_entry_t *fd_get(int fd)
{
    if (fd < 0 || fd >= FD_MAX)
    {
        return (void *)0;
    }

    if (fd_table[fd].type == FD_TYPE_NONE)
    {
        return (void *)0;
    }

    return &fd_table[fd];
}

int fd_free(int fd)
{
    if (fd < 0 || fd >= FD_MAX)
    {
        return -1;
    }

    if (fd_table[fd].type == FD_TYPE_NONE)
    {
        return -1;
    }

    if (fd_table[fd].type == FD_TYPE_FILE)
    {
        vfs_close(&fd_table[fd].file);
    }

    fd_table[fd].type = FD_TYPE_NONE;
    return 0;
}
