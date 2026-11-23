#pragma once

#include <sys/types.h>
#include <time.h>

/**
 * @file sys/stat.h
 * @brief file metadata: stat() and fstat().
 *
 * struct stat and the S_ bits are ABI and must match the kernel side in
 * src/kernel/vfs/stat.h exactly.
 */

#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000

#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

// permission bits
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

struct stat
{
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim; // last access
    struct timespec st_mtim; // last change of the data
    struct timespec st_ctim; // last change of the data or the metadata
};

// the whole seconds, by their older names
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

/**
 * get a file's metadata by path.
 * @return 0, or -1 (errno set).
 */
int stat(const char *path, struct stat *st);

/**
 * the metadata of a symbolic link itself (stat gives what it points at).
 */
int lstat(const char *path, struct stat *st);

/**
 * get an open file's metadata (pipes and the console too).
 * @return 0, or -1 (errno set).
 */
int fstat(int fd, struct stat *st);

#define S_ISUID 04000 // run with the owner's user id
#define S_ISGID 02000 // run with the file's group id
#define S_ISVTX 01000 // sticky directory

/**
 * create a directory with a mode (taken through the umask).
 */
int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *path, mode_t mode);
int fstatat(int dirfd, const char *path, struct stat *st, int flags);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);

/**
 * change the permission bits of a file (owner or root only).
 */
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);

/**
 * set the mask taken off the mode of new files.
 * @return the old mask.
 */
mode_t umask(mode_t mask);

// utimensat and futimens: a tv_nsec for the time now, or for no change
#define UTIME_NOW  ((1l << 30) - 1)
#define UTIME_OMIT ((1l << 30) - 2)

/**
 * set a file's access and modification times (times[0], times[1]); with
 * times NULL, both to the time now.
 * @param flags AT_SYMLINK_NOFOLLOW: the link itself.
 * @return 0, or -1 (errno set).
 */
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
int futimens(int fd, const struct timespec times[2]);
