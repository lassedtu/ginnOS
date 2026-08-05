#pragma once

#include "../../common/stdint.h"
#include "../../drivers/disk/block_device.h"

#define EXT2_SUPERBLOCK_OFFSET 1024u  // byte offset of superblock from start of partition
#define EXT2_SUPERBLOCK_MAGIC 0xEF53u // magic number in superblock to identify ext2 filesystem
#define EXT2_NDIR_BLOCKS 12u          // number of direct block pointers in an inode

#define EXT2_INODE_ROOT 2u // inode number of the root directory

#define EXT2_FT_REG_FILE 1u // file type value for regular files in directory entries
#define EXT2_FT_DIR 2u      // file type value for directories in directory entries

#define EXT2_S_IFREG 0x8000u // file type value for regular files in inode mode field
#define EXT2_S_IFDIR 0x4000u // file type value for directories in inode mode field
#define EXT2_S_IFMT 0xF000u  // bitmask for extracting file type from inode mode field

#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001u // incompatible feature set flag for compression support
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002u    // incompatible feature set flag for file type support
#define EXT2_FEATURE_INCOMPAT_RECOVER 0x0004u     // incompatible feature set flag for journal recovery support
#define EXT2_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008u // incompatible feature set flag for journal device support
#define EXT2_FEATURE_INCOMPAT_META_BG 0x0010u     // incompatible feature set flag for meta block group support
#define EXT2_FEATURE_INCOMPAT_EXTENTS 0x0040u     // incompatible feature set flag for extent support
#define EXT2_FEATURE_INCOMPAT_64BIT 0x0080u       // incompatible feature set flag for 64-bit support
#define EXT2_FEATURE_INCOMPAT_MMP 0x0100u         // incompatible feature set flag for multiple mount protection support
#define EXT2_FEATURE_INCOMPAT_FLEX_BG 0x0200u     // incompatible feature set flag for flexible block group support

#define EXT2_MAX_BLOCK_SIZE 4096u // maximum block size supported by ext2 filesystem (in bytes)
#define EXT2_MAX_INODE_SIZE 256u  // maximum inode size supported by ext2 filesystem (in bytes)

typedef struct __attribute__((packed))
{
    uint32_t s_inodes_count;      // total number of inodes in file system
    uint32_t s_blocks_count;      // total number of blocks in file system
    uint32_t s_r_blocks_count;    // number of reserved blocks for superuser
    uint32_t s_free_blocks_count; // total number of unallocated blocks
    uint32_t s_free_inodes_count; // total number of unallocated inodes
    uint32_t s_first_data_block;  // block number of the block containing the superblock (also the starting block number, NOT always zero.)
    uint32_t s_log_block_size;    // log_2 (block size) - 10. (In other words, the number to shift 1,024 to the left by to obtain the block size)
    uint32_t s_log_frag_size;     // log_2 (fragment size) - 10. (In other words, the number to shift 1,024 to the left by to obtain the fragment size)
    uint32_t s_blocks_per_group;  // number of blocks in each block group
    uint32_t s_frags_per_group;   // number of fragments in each block group
    uint32_t s_inodes_per_group;  // number of inodes in each block group
    uint32_t s_mtime;             // time of last mount (POSIX time)
    uint32_t s_wtime;             // time of last write (POSIX time)
    uint16_t s_mnt_count;         // number of times volume has been mounted since last consistency check (fsck)
    uint16_t s_max_mnt_count;     // maximum number of times volume can be mounted before a consistency check (fsck) is forced
    uint16_t s_magic;             // ext2 signature (0xef53), used to help confirm the presence of ext2 on a volume
    uint16_t s_state;             // file system state
    uint16_t s_errors;            // behavior when detecting errors
    uint16_t s_minor_rev_level;   // minor portion of version (combine with major portion to construct full version field)
    uint32_t s_lastcheck;         // POSIX time of last consistency check (fsck)
    uint32_t s_checkinterval;     // interval (in POSIX time) between forced consistency checks (fsck)
    uint32_t s_creator_os;        // operating system ID from which the filesystem on this volume was created
    uint32_t s_rev_level;         // major portion of version (combine with minor portion to construct full version field)
    uint16_t s_def_resuid;        // user ID that can use reserved blocks
    uint16_t s_def_resgid;        // group ID that can use reserved blocks
    uint32_t s_first_ino;         // first non-reserved inode (usually 11, but can be larger if the superblock is backed up)
    uint16_t s_inode_size;        // size of each inode structure
    uint16_t s_block_group_nr;    // block group number of this superblock
    uint32_t s_feature_compat;    // compatible feature set flags
    uint32_t s_feature_incompat;  // incompatible feature set flags
    uint32_t s_feature_ro_compat; // read-only compatible feature set flags
} EXT2_SUPERBLOCK;

typedef struct __attribute__((packed))
{
    uint32_t bg_block_bitmap;      // block number of the block bitmap for this block group
    uint32_t bg_inode_bitmap;      // block number of the inode bitmap for this block group
    uint32_t bg_inode_table;       // block number of the starting block of the inode table for this block group
    uint16_t bg_free_blocks_count; // number of unallocated blocks in this block group
    uint16_t bg_free_inodes_count; // number of unallocated inodes in this block group
    uint16_t bg_used_dirs_count;   // number of allocated inodes that are directories in this block group
    uint16_t bg_pad;               // padding to align to 4-byte boundary
    uint8_t bg_reserved[12];       // reserved for future use
} EXT2_BLOCK_GROUP_DESC;

typedef struct __attribute__((packed))
{
    uint16_t i_mode;        // file type and access permissions
    uint16_t i_uid;         // user ID of the owner
    uint32_t i_size;        // size of the file in bytes
    uint32_t i_atime;       // time of last access (POSIX time)
    uint32_t i_ctime;       // time of last status change (POSIX time)
    uint32_t i_mtime;       // time of last modification (POSIX time)
    uint32_t i_dtime;       // time of deletion (POSIX time)
    uint16_t i_gid;         // group ID of the owner
    uint16_t i_links_count; // number of hard links to the file
    uint32_t i_blocks;      // number of 512-byte blocks allocated to the file
    uint32_t i_flags;       // file flags
    uint32_t i_osd1;        // OS-dependent value
    uint32_t i_block[15];   // pointers to the blocks containing the file's data (12 direct, 1 single indirect, 1 double indirect, 1 triple indirect)
    uint32_t i_generation;  // file version (used by NFS)
    uint32_t i_file_acl;    // pointer to extended attribute block
    uint32_t i_dir_acl;     // pointer to extended attribute block (for directories) or high 32 bits of file size (for regular files)
    uint32_t i_faddr;       // pointer to the fragment address (used for filesystems with fragments)
    uint8_t i_osd2[12];     // OS-dependent value
} EXT2_INODE;

typedef struct __attribute__((packed))
{
    uint32_t inode;    // inode number of the file or directory entry
    uint16_t rec_len;  // length of this directory entry record in bytes
    uint8_t name_len;  // length of the name field in bytes
    uint8_t file_type; // type of the file or directory (e.g., regular file, directory, symbolic link)
} EXT2_DIR_ENTRY;

typedef struct
{
    BLOCK_DEVICE *disk;         // pointer to the block device used by this filesystem
    uint32_t block_size;        // size of each block in bytes (calculated as 1024 << s_log_block_size)
    uint32_t sectors_per_block; // number of sectors in each block (calculated as block_size / EXT2_SECTOR_SIZE)
    uint32_t inode_size;        // size of each inode structure in bytes (from s_inode_size, defaulting to 128 if zero)
    uint32_t first_data_block;  // block number of the first data block in the filesystem (from s_first_data_block)
    uint32_t block_group_count; // total number of block groups in the filesystem (calculated from s_blocks_count and s_blocks_per_group)
    uint32_t blocks_per_group;  // number of blocks in each block group (from s_blocks_per_group)
    uint32_t inodes_per_group;  // number of inodes in each block group (from s_inodes_per_group)
    uint32_t bgdt_start_block;  // block number of the starting block of the block group descriptor table (calculated as first_data_block + 1)
} EXT2_VOLUME;

typedef struct
{
    uint32_t inode;         // inode number of the file or directory
    uint32_t size;          // size of the file in bytes
    uint32_t cursor;        // current position in the file for reading or writing
    uint8_t file_type;      // type of the file (e.g., regular file, directory)
    uint8_t is_open;        // flag indicating whether the file is open
    EXT2_VOLUME *volume;    // pointer to the EXT2_VOLUME structure representing the filesystem volume
    EXT2_INODE inode_cache; // cached inode structure for the file, used to avoid repeated inode reads
} EXT2_FILE;

typedef struct
{
    uint32_t inode;    // inode number of the file or directory entry
    uint8_t file_type; // type of the file or directory (e.g., regular file, directory)
    uint32_t size;     // size of the file in bytes (for regular files) or number of entries (for directories)
    char name[256];    // null-terminated name of the file or directory entry (maximum length of 255 characters plus null terminator)
} EXT2_DIRECTORY_ENTRY;

/**
 * initialize an ext2 volume from a disk.
 * @param volume volume object to initialize.
 * @param disk initialized block device backend.
 * @return true on success. false on failure.
 */
bool EXT2_Initialize(EXT2_VOLUME *volume, BLOCK_DEVICE *disk);

/**
 * read one inode by inode number.
 * @param volume initialized ext2 volume.
 * @param inode_number inode to read.
 * @param inode_out output inode.
 * @return true on success. false on failure.
 */
bool EXT2_ReadInode(EXT2_VOLUME *volume, uint32_t inode_number, EXT2_INODE *inode_out);

/**
 * list entries in a directory inode.
 * @param volume initialized ext2 volume.
 * @param inode_number directory inode number.
 * @return true on success. false on failure.
 */
bool EXT2_ListDirectory(EXT2_VOLUME *volume, uint32_t inode_number);

/**
 * read bytes from a file inode into buffer.
 * @param volume initialized ext2 volume.
 * @param inode_number file inode number.
 * @param offset byte offset in file.
 * @param length bytes to read.
 * @param buffer destination buffer.
 * @return true on success. false on failure.
 */
bool EXT2_ReadFile(EXT2_VOLUME *volume, uint32_t inode_number, uint32_t offset, uint32_t length, void *buffer);

/**
 * resolve an absolute path to an inode.
 * @param volume initialized ext2 volume.
 * @param path absolute path.
 * @param inode_out resolved inode.
 * @return true on success. false on failure.
 */
bool EXT2_LookupPath(EXT2_VOLUME *volume, const char *path, uint32_t *inode_out);

/**
 * open a file or directory by absolute path.
 * @param volume initialized ext2 volume.
 * @param path absolute path.
 * @param file output file handle.
 * @return true on success. false on failure.
 */
bool EXT2_Open(EXT2_VOLUME *volume, const char *path, EXT2_FILE *file);

/**
 * read bytes from an open ext2 file handle.
 * @param file open file handle.
 * @param byteCount max bytes to read.
 * @param dataOut destination buffer.
 * @return number of bytes read.
 */
uint32_t EXT2_Read(EXT2_FILE *file, uint32_t byteCount, void *dataOut);

/**
 * read one directory entry from an open directory handle.
 * @param file open directory handle.
 * @param entryOut output directory entry.
 * @return true when an entry is read. false when done or on failure.
 */
bool EXT2_ReadEntry(EXT2_FILE *file, EXT2_DIRECTORY_ENTRY *entryOut);

/**
 * close an open ext2 file handle.
 * @param file file handle to close.
 */
void EXT2_Close(EXT2_FILE *file);
