#pragma once

#include "../fs/fs.h"

typedef FS_STAT VFS_STAT;

/**
 * virtual file system operation status codes.
 */
typedef enum
{
    VFS_OK = FS_OK,                               // operation completed successfully
    VFS_NOT_FOUND = FS_NOT_FOUND,                 // file or directory not found
    VFS_PERMISSION_DENIED = FS_PERMISSION_DENIED, // permission denied for the operation
    VFS_IO_ERROR = FS_IO_ERROR,                   // I/O error occurred during the operation
} VFS_STATUS;

/**
 * virtual file system file handle structure representing an open file or directory.
 * this structure is used to abstract the underlying filesystem implementation.
 */
typedef struct
{
    FS_MOUNT *mount;
    FS_FILE file;
} VFS_FILE;

/**
 * mount a filesystem as the root filesystem.
 * @param mount pointer to the filesystem mount structure to be used as the root filesystem.
 * @return true on success, false on failure.
 */
bool vfs_mount_root(FS_MOUNT *mount);

/**
 * open a file or directory by absolute path in the virtual file system.
 * @param path absolute path to the file or directory.
 * @param file pointer to a VFS_FILE structure that will be initialized with the opened file
 * @return true on success, false on failure.
 */
bool vfs_open(
    const char *path,
    VFS_FILE *file);

/**
 * create a regular file by absolute path.
 * @param path absolute path to the new file.
 * @return true on success, false on failure.
 */
bool vfs_create(const char *path);

/**
 * create a directory by absolute path.
 * @param path absolute path to the new directory.
 * @return true on success, false on failure.
 */
bool vfs_mkdir(const char *path);

/**
 * remove a file by absolute path.
 * @param path absolute path to the file to remove.
 * @return true on success, false on failure.
 */
bool vfs_remove(const char *path);

/**
 * remove a directory by absolute path.
 * @param path absolute path to the directory to remove.
 * @return true on success, false on failure.
 */
bool vfs_rmdir(const char *path);

/**
 * rename a file or directory by absolute paths.
 * @param old_path absolute path to the existing file or directory.
 * @param new_path absolute path to the new name for the file or directory.
 * @return true on success, false on failure.
 */
bool vfs_rename(const char *old_path, const char *new_path);

/**
 * stat a file or directory by absolute path.
 * @param path absolute path to the file or directory.
 * @param stat_out pointer to a VFS_STAT structure that will be filled with the file's metadata.
 * @return VFS_OK on success, or an error code on failure.
 */
VFS_STATUS vfs_stat(const char *path, VFS_STAT *stat_out);

/**
 * resolve an input path against the current working directory.
 * absolute paths are returned as-is, relative paths are prefixed with cwd.
 * @param cwd current working directory.
 * @param input user-provided path.
 * @param output destination buffer for resolved absolute path.
 * @param size size of destination buffer in bytes.
 * @return true on success, false if inputs are invalid or output would overflow.
 */
bool vfs_resolve_path(
    const char *cwd,
    const char *input,
    char *output,
    uint32_t size);

/**
 * join two path components into an output buffer.
 * avoids duplicate separators when base already ends with '/'.
 * @param base base path component.
 * @param name child path component.
 * @param out destination buffer.
 * @param size destination buffer size in bytes.
 * @return true on success, false on invalid input or insufficient output size.
 */
bool vfs_join_path(
    const char *base,
    const char *name,
    char *out,
    uint32_t size);

/**
 * read data from an open file in the virtual file system.
 * @param file pointer to the VFS_FILE structure representing the open file.
 * @param size number of bytes to read.
 * @param buffer pointer to the buffer where the read data will be stored.
 * @return number of bytes actually read, or 0 on failure.
 */
uint32_t vfs_read(
    VFS_FILE *file,
    uint32_t size,
    void *buffer);

/**
 * read a directory entry from an open directory in the virtual file system.
 * @param file pointer to the VFS_FILE structure representing the open directory.
 * @param entryOut pointer to output directory entry.
 * @return true on success, false on failure.
 */
bool vfs_read_entry(
    VFS_FILE *file,
    FS_DIRENT *entryOut);

/**
 * close an open file in the virtual file system.
 * @param file pointer to the VFS_FILE structure representing the open file.
 * @return void
 */
void vfs_close(
    VFS_FILE *file);

/**
 * get the type of an open file in the virtual file system.
 * @param file pointer to the VFS_FILE structure representing the open file.
 * @return file type (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN).
 */
uint8_t vfs_file_type(
    VFS_FILE *file);