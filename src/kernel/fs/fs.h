#pragma once

#include "common/stdint.h"
#include "arch/arch.h"
#include "common/error.h"
#include "drivers/disk/block_device.h"
#include "fs/ext2/ext2.h"

/**
 * file system types
 */
enum
{
    FS_TYPE_UNKNOWN = 0, // unknown or unsupported file type
    FS_TYPE_FILE = 1,    // regular file
    FS_TYPE_DIR = 2,     // directory
    FS_TYPE_SYMLINK = 3, // symbolic link
};

/**
 * filesystem operations vtable.
 *
 * each filesystem type (ext2 today; procfs/tmpfs/FAT32 later) provides one of
 * these so the generic fs_* layer never names a concrete filesystem. a mount
 * points at its filesystem's ops; every open file carries the same pointer so
 * file-level calls dispatch without consulting the mount again.
 *
 * mount is the only entry point that is not in the vtable: fs_mount() selects
 * the filesystem (currently always ext2) and installs its ops.
 */
typedef struct fs_mount fs_mount_t;
typedef struct fs_file fs_file_t;
typedef struct fs_dirent fs_dirent_t;
typedef struct fs_stat fs_stat_t;

// which fields of an fs_attr_t to set
#define FS_ATTR_MODE  0x1 // the permission bits (07777)
#define FS_ATTR_OWNER 0x2 // uid and gid
#define FS_ATTR_ATIME 0x4 // the time of the last access
#define FS_ATTR_MTIME 0x8 // the time of the last change of the data

/**
 * a change of a file's permissions or owner (setattr).
 */
typedef struct
{
    uint32_t what; // FS_ATTR_* bits
    uint32_t mode; // permission bits; the file type is kept
    uint32_t uid;
    uint32_t gid;
    uint32_t atime; // seconds since 1970
    uint32_t mtime;
} fs_attr_t;

typedef struct
{
    kerr_t (*open)(fs_mount_t *mount, const char *path, fs_file_t *file);
    kerr_t (*create)(fs_mount_t *mount, const char *path);
    kerr_t (*mkdir)(fs_mount_t *mount, const char *path);
    kerr_t (*remove)(fs_mount_t *mount, const char *path);
    kerr_t (*rmdir)(fs_mount_t *mount, const char *path);
    kerr_t (*rename)(fs_mount_t *mount, const char *old_path, const char *new_path);
    kerr_t (*stat)(fs_mount_t *mount, const char *path, fs_stat_t *stat_out);

    uint32_t (*read)(fs_file_t *file, uint32_t byte_count, void *data_out);
    uint32_t (*write)(fs_file_t *file, uint32_t byte_count, const void *data_in);
    kerr_t (*truncate)(fs_file_t *file, uint32_t length); // cut or grow to length
    kerr_t (*read_entry)(fs_file_t *file, fs_dirent_t *entry_out);
    kerr_t (*fstat)(fs_file_t *file, fs_stat_t *stat_out);
    /**
     * change the permission bits and/or owner of an open file (optional;
     * NULL means the filesystem keeps no owners: KERR_PERM).
     */
    kerr_t (*setattr)(fs_file_t *file, const fs_attr_t *attr);
    void (*close)(fs_file_t *file);

    /**
     * device-control operation for a special file (optional; NULL means the
     * filesystem supports no ioctls, i.e. -ENOTTY). devfs forwards this to the
     * backing device; disk filesystems like ext2 leave it NULL. returns 0 on
     * success or a negative errno.
     */
    int32_t (*ioctl)(fs_file_t *file, uint32_t request, void *arg);

    /**
     * map an open special file's memory into a process (optional; NULL means
     * the file is not mappable, i.e. -ENODEV). devfs forwards this to the
     * backing device; disk filesystems leave it NULL. returns 0 or -errno.
     */
    int32_t (*mmap)(fs_file_t *file, addr_space_t as, uintptr_t virt,
                    size_t length, int prot, uint32_t offset);

    /**
     * reposition an open file's cursor (optional). devfs implements this over
     * its generic cursor; ext2 leaves it NULL and the syscall layer uses the
     * ext2 file cursor directly. whence is SEEK_SET(0)/CUR(1)/END(2).
     * @return the new absolute position, or a negative errno.
     */
    int32_t (*seek)(fs_file_t *file, int32_t offset, int whence);

    /**
     * links (all optional; NULL means the filesystem has none). find_link
     * reports the first symbolic link along a path (see ext2_find_link);
     * readlink reads one's target; symlink makes one; link gives a file
     * one more name.
     */
    kerr_t (*find_link)(fs_mount_t *mount, const char *path, bool follow_last, char *target,
                        uint32_t size, uint32_t *end_out);
    kerr_t (*readlink)(fs_mount_t *mount, const char *path, char *target, uint32_t size);
    kerr_t (*symlink)(fs_mount_t *mount, const char *path, const char *target);
    kerr_t (*link)(fs_mount_t *mount, const char *old_path, const char *new_path);
} fs_ops_t;

/**
 * directory entry structure for reading directory contents.
 */
struct fs_dirent
{
    uint32_t inode;    // inode number of the file or directory
    uint8_t file_type; // type of the file
    uint32_t size;     // size of the file in bytes
    char name[EXT2_NAME_MAX]; // null-terminated name of the file or directory (max 255 characters)
};

/**
 * filesystem mount structure representing a mounted filesystem.
 */
struct fs_mount
{
    const fs_ops_t *ops;  // filesystem operations vtable (set by fs_mount)
    ext2_volume_t ext2;   // embedded ext2_volume_t representing the mounted filesystem (not a pointer)
    void *fs_data;        // generic per-mount state for non-ext2 filesystems (devfs, procfs, ...)
    uint8_t is_mounted; // flag indicating whether the filesystem is successfully mounted (1 for mounted, 0 for not mounted)
};

/**
 * filesystem metadata structure returned by stat.
 */
struct fs_stat
{
    uint32_t inode;
    uint8_t file_type;
    uint16_t mode;
    uint16_t links_count;
    uint32_t size;
    uint32_t blocks;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint16_t uid;
    uint16_t gid;
    uint32_t rdev; // a device node's device number (DEVICE_NUMBER), or 0
};

/**
 * file handle structure representing an open file or directory.
 */
struct fs_file
{
    const fs_ops_t *ops;   // vtable of the filesystem this file belongs to
    ext2_file_t ext2_file; // embedded ext2_file_t representing the open file or directory (not a pointer)
    void *fs_data;         // generic per-file state for non-ext2 filesystems (e.g. the target device_t)
    uint32_t fs_pos;       // generic cursor for non-ext2 filesystems (e.g. devfs dir index)
    uint8_t file_type;   // type of the file (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN)
    uint8_t is_open;     // flag indicating whether the file is open (1 for open, 0 for closed)
};

/**
 * mount a filesystem on a block device.
 * @param mount filesystem mount object to initialize.
 * @param device initialized block device backend.
 * @return true on success. false on failure.
 */
bool fs_mount(fs_mount_t *mount, block_device_t *device);

/**
 * open a file or directory by absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file or directory.
 * @param file output file handle.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_open(fs_mount_t *mount, const char *path, fs_file_t *file);

/**
 * create a regular file at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the new file.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_create(fs_mount_t *mount, const char *path);

/**
 * create a directory at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the new directory.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_mkdir(fs_mount_t *mount, const char *path);

/**
 * remove a file at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file to remove.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_remove(fs_mount_t *mount, const char *path);

/**
 * remove a directory at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the directory to remove.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_rmdir(fs_mount_t *mount, const char *path);

/**
 * rename a file or directory.
 * @param mount initialized filesystem mount.
 * @param old_path absolute path to the existing file or directory.
 * @param new_path absolute path to the new name for the file or directory.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_rename(fs_mount_t *mount, const char *old_path, const char *new_path);

/**
 * stat a file or directory by absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file or directory.
 * @param stat_out output stat structure.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_stat(fs_mount_t *mount, const char *path, fs_stat_t *stat_out);

/**
 * the link operations (see fs_ops_t). with no such operation: find_link
 * finds none, readlink is KERR_INVAL (not a link), symlink and link are
 * KERR_PERM.
 */
kerr_t fs_find_link(fs_mount_t *mount, const char *path, bool follow_last, char *target,
                    uint32_t size, uint32_t *end_out);
kerr_t fs_readlink(fs_mount_t *mount, const char *path, char *target, uint32_t size);
kerr_t fs_symlink(fs_mount_t *mount, const char *path, const char *target);
kerr_t fs_link(fs_mount_t *mount, const char *old_path, const char *new_path);

/**
 * read bytes from an open file into a buffer.
 * @param file open file handle.
 * @param byteCount number of bytes to read.
 * @param dataOut destination buffer.
 * @return number of bytes actually read.
 */
uint32_t fs_read(fs_file_t *file, uint32_t byteCount, void *dataOut);

/**
 * write bytes to an open file.
 * @param file open file handle.
 * @param byteCount number of bytes to write.
 * @param dataIn source buffer.
 * @return number of bytes actually written, or 0 on failure.
 */
uint32_t fs_write(fs_file_t *file, uint32_t byteCount, const void *dataIn);

/**
 * cut or grow an open file to length bytes (growth reads as zeros).
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_truncate(fs_file_t *file, uint32_t length);

/**
 * stat an open file or directory.
 * @param file open file handle.
 * @param stat_out output stat structure.
 * @return KERR_OK, or KERR_INVAL if the filesystem cannot stat open files.
 */
kerr_t fs_fstat(fs_file_t *file, fs_stat_t *stat_out);

/**
 * change an open file's permission bits and/or owner.
 * @param file open file handle.
 * @param attr what to change.
 * @return KERR_OK, or KERR_PERM if the filesystem keeps no owners.
 */
kerr_t fs_setattr(fs_file_t *file, const fs_attr_t *attr);

/**
 * read a directory entry from an open directory file.
 * @param file open directory file handle.
 * @param entryOut output directory entry.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_read_entry(fs_file_t *file, fs_dirent_t *entryOut);

/**
 * close an open file or directory.
 * @param file open file handle to close.
 * @return void
 */
void fs_close(fs_file_t *file);

/**
 * get the type of an open file or directory.
 * @param file open file handle.
 * @return file type (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN).
 */
uint8_t fs_file_type(const fs_file_t *file);

/**
 * issue a device-control (ioctl) request on an open special file.
 * dispatches through the filesystem's ioctl op; filesystems without one
 * (ext2, etc.) return -ENOTTY.
 * @param file open file handle.
 * @param request driver-defined request code.
 * @param arg request-specific argument (typically a bounds-checked pointer).
 * @return 0 on success, or a negative errno on failure.
 */
int32_t fs_ioctl(fs_file_t *file, uint32_t request, void *arg);

/**
 * map an open special file's memory into a process address space.
 * dispatches through the filesystem's mmap op; filesystems without one return
 * -ENODEV. the frames are mapped eagerly at @p virt in @p page_directory.
 * @param file open file handle.
 * @param page_directory physical address of the target process page directory.
 * @param virt page-aligned destination virtual address.
 * @param length bytes to map (page-multiple).
 * @param prot PROT_* bits.
 * @param offset page-aligned byte offset into the mapped object.
 * @return 0 on success, or a negative errno on failure.
 */
int32_t fs_mmap(fs_file_t *file, addr_space_t as, uintptr_t virt,
                size_t length, int prot, uint32_t offset);

/**
 * reposition an open file's cursor via the filesystem's seek op.
 * @return the new absolute position, or -ENOSYS if the filesystem has no seek
 *         op (the caller then handles seeking itself, e.g. via the ext2
 *         cursor), or another negative errno on failure.
 */
int32_t fs_seek(fs_file_t *file, int32_t offset, int whence);
