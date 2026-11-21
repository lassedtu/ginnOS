#pragma once

/**
 * struct stat as user space sees it (stat, fstat). the layout and the
 * S_ bits are ABI: the libc's <sys/stat.h> has the same ones.
 */

#include "common/stdint.h"
#include "kernel/fs/fs.h"

// file type bits of st_mode (the ext2 i_mode values)
#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000

struct kstat
{
    uint32_t st_dev;     // device of the filesystem (0 for now)
    uint32_t st_ino;     // inode number
    uint32_t st_mode;    // S_IF* type and permission bits
    uint32_t st_nlink;   // hard links
    uint32_t st_uid;     // owner
    uint32_t st_gid;     // group
    uint32_t st_rdev;    // device, for a device node (0 for now)
    int32_t st_size;     // bytes
    uint32_t st_blksize; // preferred I/O size
    uint32_t st_blocks;  // 512-byte blocks allocated
    uint32_t st_atime;   // last access, seconds since 1970
    uint32_t st_mtime;   // last modification
    uint32_t st_ctime;   // last status change
};

/**
 * fill a struct kstat from a filesystem's stat. a filesystem that leaves
 * the type bits of the mode out gets them from its file type.
 * @param fs the filesystem's stat.
 * @param out receives the user-facing stat.
 */
static inline void stat_from_fs(const fs_stat_t *fs, struct kstat *out)
{
    uint32_t mode = fs->mode;
    if (!(mode & S_IFMT))
    {
        mode |= fs->file_type == FS_TYPE_DIR ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    }

    out->st_dev = 0;
    out->st_ino = fs->inode;
    out->st_mode = mode;
    out->st_nlink = fs->links_count;
    out->st_uid = fs->uid;
    out->st_gid = fs->gid;
    out->st_rdev = 0;
    out->st_size = (int32_t)fs->size;
    out->st_blksize = 1024;
    out->st_blocks = fs->blocks;
    out->st_atime = fs->atime;
    out->st_mtime = fs->mtime;
    out->st_ctime = fs->ctime;
}
