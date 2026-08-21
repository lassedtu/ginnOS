#pragma once

#include "common/stdint.h"
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
};

/**
 * directory entry structure for reading directory contents.
 */
typedef struct
{
    uint32_t inode;    // inode number of the file or directory
    uint8_t file_type; // type of the file
    uint32_t size;     // size of the file in bytes
    char name[256];    // null-terminated name of the file or directory (max 255 characters)
} fs_dirent_t;

/**
 * filesystem mount structure representing a mounted filesystem.
 */
typedef struct
{
    ext2_volume_t ext2;   // embedded ext2_volume_t representing the mounted filesystem (not a pointer)
    uint8_t is_mounted; // flag indicating whether the filesystem is successfully mounted (1 for mounted, 0 for not mounted)
} fs_mount_t;

/**
 * filesystem metadata structure returned by stat.
 */
typedef struct
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
} fs_stat_t;

/**
 * file handle structure representing an open file or directory.
 */
typedef struct
{
    ext2_file_t ext2_file; // embedded ext2_file_t representing the open file or directory (not a pointer)
    uint8_t file_type;   // type of the file (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN)
    uint8_t is_open;     // flag indicating whether the file is open (1 for open, 0 for closed)
} fs_file_t;

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
 * truncate an open file to zero length.
 * @param file open file handle.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t fs_truncate(fs_file_t *file);

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
