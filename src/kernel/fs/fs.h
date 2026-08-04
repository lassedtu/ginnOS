#pragma once

#include "../../common/stdint.h"
#include "../../drivers/disk/block_device.h"
#include "../../fs/ext2/ext2.h"

enum
{
    FS_TYPE_UNKNOWN = 0,
    FS_TYPE_FILE = 1,
    FS_TYPE_DIR = 2,
};

typedef struct
{
    uint32_t inode;
    uint8_t file_type;
    uint32_t size;
    char name[256];
} FS_DIRENT;

typedef struct
{
    EXT2_VOLUME ext2;
    uint8_t is_mounted;
} FS_MOUNT;

typedef struct
{
    EXT2_FILE ext2_file;
    uint8_t file_type;
    uint8_t is_open;
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
 * read bytes from an open file into a buffer.
 * @param file open file handle.
 * @param byteCount number of bytes to read.
 * @param dataOut destination buffer.
 * @return number of bytes actually read.
 */
uint32_t fs_read(FS_FILE *file, uint32_t byteCount, void *dataOut);

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
