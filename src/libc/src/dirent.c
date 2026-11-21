#include "dirent.h"

#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

// bytes of getdents records a stream buffers
#define DIR_BUFFER_SIZE 1024

// the bytes before d_name in a getdents record
#define RECORD_HEADER 7

struct dir_stream
{
    int fd;
    int used;     // bytes of records in buffer
    int position; // offset of the next record
    struct dirent entry;
    unsigned char buffer[DIR_BUFFER_SIZE];
};

int getdents(int fd, void *buf, unsigned int size)
{
    int ret = _syscall(SYS_GETDENTS, fd, (long)buf, (long)size, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        return NULL;
    }

    DIR *dir = malloc(sizeof(DIR));
    if (!dir)
    {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    dir->fd = fd;
    dir->used = 0;
    dir->position = 0;
    return dir;
}

struct dirent *readdir(DIR *dir)
{
    if (dir->position >= dir->used)
    {
        int n = getdents(dir->fd, dir->buffer, sizeof(dir->buffer));
        if (n <= 0)
        {
            return NULL;
        }
        dir->used = n;
        dir->position = 0;
    }

    // the record: d_ino (4 bytes), d_reclen (2), d_type (1), d_name
    unsigned char *record = dir->buffer + dir->position;
    unsigned short reclen;
    memcpy(&dir->entry.d_ino, record, 4);
    memcpy(&reclen, record + 4, 2);
    dir->entry.d_type = record[6];
    strncpy(dir->entry.d_name, (const char *)record + RECORD_HEADER,
            sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';

    dir->position += reclen;
    return &dir->entry;
}

int closedir(DIR *dir)
{
    int ret = close(dir->fd);
    free(dir);
    return ret;
}

int dirfd(DIR *dir)
{
    return dir->fd;
}
