#pragma once

#include "common/error.h"
#include "kernel/fs/fs.h"

typedef fs_stat_t vfs_stat_t;

/**
 * virtual file system file handle structure representing an open file or directory.
 * this structure is used to abstract the underlying filesystem implementation.
 */
typedef struct
{
    fs_mount_t *mount;
    fs_file_t file;
} vfs_file_t;

/**
 * mount a filesystem as the root filesystem.
 * @param mount pointer to the filesystem mount structure to be used as the root filesystem.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_mount_root(fs_mount_t *mount);

/**
 * open a file or directory by absolute path in the virtual file system.
 * @param path absolute path to the file or directory.
 * @param file pointer to a vfs_file_t structure that will be initialized with the opened file
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_open(
    const char *path,
    vfs_file_t *file);

/**
 * create a regular file by absolute path.
 * @param path absolute path to the new file.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_create(const char *path);

/**
 * create a directory by absolute path.
 * @param path absolute path to the new directory.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_mkdir(const char *path);

/**
 * remove a file by absolute path.
 * @param path absolute path to the file to remove.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_remove(const char *path);

/**
 * remove a directory by absolute path.
 * @param path absolute path to the directory to remove.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_rmdir(const char *path);

/**
 * rename a file or directory by absolute paths.
 * @param old_path absolute path to the existing file or directory.
 * @param new_path absolute path to the new name for the file or directory.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_rename(const char *old_path, const char *new_path);

/**
 * stat a file or directory by absolute path.
 * @param path absolute path to the file or directory.
 * @param stat_out pointer to a vfs_stat_t structure that will be filled with the file's metadata.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_stat(const char *path, vfs_stat_t *stat_out);

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
 * @param file pointer to the vfs_file_t structure representing the open file.
 * @param size number of bytes to read.
 * @param buffer pointer to the buffer where the read data will be stored.
 * @return number of bytes actually read, or 0 on failure.
 */
uint32_t vfs_read(
    vfs_file_t *file,
    uint32_t size,
    void *buffer);

/**
 * write data to an open file in the virtual file system.
 * @param file pointer to the vfs_file_t structure representing the open file.
 * @param size number of bytes to write.
 * @param buffer pointer to the source data.
 * @return number of bytes actually written, or 0 on failure.
 */
uint32_t vfs_write(
    vfs_file_t *file,
    uint32_t size,
    const void *buffer);

/**
 * truncate an open file to zero length.
 * @param file pointer to the vfs_file_t structure.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_truncate(vfs_file_t *file);

/**
 * read a directory entry from an open directory in the virtual file system.
 * @param file pointer to the vfs_file_t structure representing the open directory.
 * @param entryOut pointer to output directory entry.
 * @return KERR_OK on success, or an error code on failure.
 */
kerr_t vfs_read_entry(
    vfs_file_t *file,
    fs_dirent_t *entryOut);

/**
 * close an open file in the virtual file system.
 * @param file pointer to the vfs_file_t structure representing the open file.
 * @return void
 */
void vfs_close(
    vfs_file_t *file);

/**
 * get the type of an open file in the virtual file system.
 * @param file pointer to the vfs_file_t structure representing the open file.
 * @return file type (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN).
 */
uint8_t vfs_file_type(
    vfs_file_t *file);