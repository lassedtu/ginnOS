#pragma once

/**
 * @file dirent.h
 * @brief reading directories: opendir, readdir, closedir.
 *
 * built on the getdents syscall, whose record layout and DT_ values are ABI
 * (src/kernel/vfs/dirent.h).
 */

// d_type values
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8

struct dirent
{
    unsigned int d_ino;
    unsigned char d_type;
    char d_name[256];
};

// an open directory stream (its fields are private to the libc)
typedef struct dir_stream DIR;

/**
 * open a directory for reading.
 * @return the stream, or NULL (errno set).
 */
DIR *opendir(const char *path);

/**
 * read the next entry. the returned struct is overwritten by the next call.
 * @return the entry, or NULL at the end of the directory or on error.
 */
struct dirent *readdir(DIR *dir);

/**
 * close a directory stream and its file descriptor.
 * @return 0, or -1 (errno set).
 */
int closedir(DIR *dir);

/**
 * the file descriptor under a directory stream.
 */
int dirfd(DIR *dir);

/**
 * read raw getdents records (the syscall under readdir).
 * @return bytes read, 0 at the end, or -1 (errno set).
 */
int getdents(int fd, void *buf, unsigned int size);
