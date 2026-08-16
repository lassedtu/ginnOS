#pragma once

#include "../../common/stdint.h"
#include "../../drivers/disk/block_device.h"
#include "../../fs/ext2/ext2.h"

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
 * filesystem operation status codes.
 */
typedef enum
{
    FS_OK = 0,                // operation completed successfully
    FS_NOT_FOUND = 1,         // file or directory not found
    FS_PERMISSION_DENIED = 2, // permission denied for the operation
    FS_IO_ERROR = 3,          // I/O error occurred during the operation
} FS_STATUS;

/**
 * directory entry structure for reading directory contents.
 */
typedef struct
{
    uint32_t inode;    // inode number of the file or directory
    uint8_t file_type; // type of the file
    uint32_t size;     // size of the file in bytes
    char name[256];    // null-terminated name of the file or directory (max 255 characters)
} FS_DIRENT;

/**
 * filesystem mount structure representing a mounted filesystem.
 */
typedef struct
{
    EXT2_VOLUME ext2;   // embedded EXT2_VOLUME representing the mounted filesystem (not a pointer)
    uint8_t is_mounted; // flag indicating whether the filesystem is successfully mounted (1 for mounted, 0 for not mounted)
} FS_MOUNT;

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
} FS_STAT;

/**
 * file handle structure representing an open file or directory.
 */
typedef struct
{
    EXT2_FILE ext2_file; // embedded EXT2_FILE representing the open file or directory (not a pointer)
    uint8_t file_type;   // type of the file (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN)
    uint8_t is_open;     // flag indicating whether the file is open (1 for open, 0 for closed)
} FS_FILE;

/**
 * mount a filesystem on a block device.
 * @param mount filesystem mount object to initialize.
 * @param device initialized block device backend.
 * @return true on success. false on failure.
 */
bool fs_mount(FS_MOUNT *mount, BLOCK_DEVICE *device);

/**
 * open a file or directory by absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file or directory.
 * @param file output file handle.
 * @return true on success. false on failure.
 */
bool fs_open(FS_MOUNT *mount, const char *path, FS_FILE *file);

/**
 * create a regular file at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the new file.
 * @return true on success. false on failure.
 */
bool fs_create(FS_MOUNT *mount, const char *path);

/**
 * create a directory at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the new directory.
 * @return true on success. false on failure.
 */
bool fs_mkdir(FS_MOUNT *mount, const char *path);

/**
 * remove a file at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file to remove.
 * @return true on success. false on failure.
 */
bool fs_remove(FS_MOUNT *mount, const char *path);

/**
 * remove a directory at an absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the directory to remove.
 * @return true on success. false on failure.
 */
bool fs_rmdir(FS_MOUNT *mount, const char *path);

/**
 * rename a file or directory.
 * @param mount initialized filesystem mount.
 * @param old_path absolute path to the existing file or directory.
 * @param new_path absolute path to the new name for the file or directory.
 * @return true on success. false on failure.
 */
bool fs_rename(FS_MOUNT *mount, const char *old_path, const char *new_path);

/**
 * stat a file or directory by absolute path.
 * @param mount initialized filesystem mount.
 * @param path absolute path to the file or directory.
 * @param stat_out output stat structure.
 * @return FS_OK on success, or an error code on failure.
 */
FS_STATUS fs_stat(FS_MOUNT *mount, const char *path, FS_STAT *stat_out);

/**
 * read bytes from an open file into a buffer.
 * @param file open file handle.
 * @param byteCount number of bytes to read.
 * @param dataOut destination buffer.
 * @return number of bytes actually read.
 */
uint32_t fs_read(FS_FILE *file, uint32_t byteCount, void *dataOut);

/**
 * write bytes to an open file.
 * @param file open file handle.
 * @param byteCount number of bytes to write.
 * @param dataIn source buffer.
 * @return number of bytes actually written, or 0 on failure.
 */
uint32_t fs_write(FS_FILE *file, uint32_t byteCount, const void *dataIn);

/**
 * truncate an open file to zero length.
 * @param file open file handle.
 * @return true on success. false on failure.
 */
bool fs_truncate(FS_FILE *file);

/**
 * read a directory entry from an open directory file.
 * @param file open directory file handle.
 * @param entryOut output directory entry.
 * @return true on success. false on failure.
 */
bool fs_read_entry(FS_FILE *file, FS_DIRENT *entryOut);

/**
 * close an open file or directory.
 * @param file open file handle to close.
 * @return void
 */
void fs_close(FS_FILE *file);

/**
 * get the type of an open file or directory.
 * @param file open file handle.
 * @return file type (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN).
 */
uint8_t fs_file_type(const FS_FILE *file);
