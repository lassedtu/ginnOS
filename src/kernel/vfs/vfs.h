#pragma once

#include "../fs/fs.h"

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
uint8_t vfs_type(
    VFS_FILE *file);

/**
 * get the type of an open file in the virtual file system.
 * @param file pointer to the VFS_FILE structure representing the open file.
 * @return file type (FS_TYPE_FILE, FS_TYPE_DIR, or FS_TYPE_UNKNOWN).
 */
uint8_t vfs_file_type(
    VFS_FILE *file);