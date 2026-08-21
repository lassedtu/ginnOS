#pragma once

#include "ext2.h"
#include "common/error.h"
#include "common/memory.h"
#include "common/string.h"
#include "common/stdio.h"

#define EXT2_SECTOR_SIZE 512u
#define EXT2_MIN(a, b) ((a) < (b) ? (a) : (b))
#define OFFSETOF(type, member) ((uint32_t)&(((type *)0)->member))

// global buffers (defined in ext2_io.c)
extern uint8_t g_sector_buffer[EXT2_SECTOR_SIZE];
extern uint8_t g_block_buffer[EXT2_MAX_BLOCK_SIZE];
extern uint8_t g_block_buffer2[EXT2_MAX_BLOCK_SIZE];
extern uint8_t g_block_buffer3[EXT2_MAX_BLOCK_SIZE];
extern uint8_t g_block_buffer4[EXT2_MAX_BLOCK_SIZE];
extern uint8_t g_inode_buffer[EXT2_MAX_INODE_SIZE];
extern uint8_t g_bitmap_buffer[EXT2_MAX_BLOCK_SIZE];

// inline utilities
static inline uint32_t ext2_align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static inline uint32_t ext2_dir_entry_size(uint32_t name_len)
{
    return ext2_align4(OFFSETOF(ext2_dir_entry_t, file_type) + 1u + name_len);
}

// ext2_io.c
bool read_abs_bytes(block_device_t *disk, uint32_t byte_offset, uint32_t size, void *out);
bool read_block(ext2_volume_t *volume, uint32_t block, void *out);
bool read_group_desc(ext2_volume_t *volume, uint32_t group, ext2_block_group_desc_t *out_desc);
bool write_abs_bytes(block_device_t *disk, uint32_t byte_offset, uint32_t size, const void *in);
bool write_block(ext2_volume_t *volume, uint32_t block, const void *in);
bool write_group_desc(ext2_volume_t *volume, uint32_t group, const ext2_block_group_desc_t *desc);
bool write_superblock(ext2_volume_t *volume);
bool read_inode_bitmap(ext2_volume_t *volume, uint32_t group, uint8_t *bitmap);
bool write_inode_bitmap(ext2_volume_t *volume, uint32_t group, const uint8_t *bitmap);
bool read_block_bitmap(ext2_volume_t *volume, uint32_t group, uint8_t *bitmap);
bool write_block_bitmap(ext2_volume_t *volume, uint32_t group, const uint8_t *bitmap);

// ext2_alloc.c
bool inode_location(ext2_volume_t *volume, uint32_t inode_number, ext2_block_group_desc_t *desc_out, uint32_t *inode_byte_offset_out, uint32_t *group_out);
bool write_inode(ext2_volume_t *volume, uint32_t inode_number, const ext2_inode_t *inode);
bool update_group_and_super_counts(ext2_volume_t *volume, uint32_t group, int32_t free_blocks_delta, int32_t free_inodes_delta, int32_t used_dirs_delta);
bool alloc_inode(ext2_volume_t *volume, uint32_t *inode_number_out);
bool free_inode(ext2_volume_t *volume, uint32_t inode_number);
bool alloc_block(ext2_volume_t *volume, uint32_t *block_number_out);
bool free_block(ext2_volume_t *volume, uint32_t block_number);
bool free_inode_block_chain(ext2_volume_t *volume, ext2_inode_t *inode);

// ext2_inode.c
bool inode_is_dir(const ext2_inode_t *inode);
bool inode_is_regular(const ext2_inode_t *inode);
bool inode_is_directory(const ext2_inode_t *inode);
uint8_t inode_to_file_type(const ext2_inode_t *inode);
kerr_t find_in_directory(ext2_volume_t *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *inode_out);
bool resolve_data_block(ext2_volume_t *volume, const ext2_inode_t *inode, uint32_t logical_block_index, uint32_t *physical_block_out);

// ext2_dir.c
bool find_directory_entry(ext2_volume_t *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *block_index_out, uint32_t *offset_out, ext2_dir_entry_t *entry_out);
bool append_or_replace_directory_entry(ext2_volume_t *volume, uint32_t parent_inode_number, ext2_inode_t *parent_inode, const char *name, uint32_t name_len, uint32_t child_inode_number, uint8_t file_type);
bool update_directory_entry_name(ext2_volume_t *volume, uint32_t dir_inode_number, const char *old_name, uint32_t old_name_len, const char *new_name, uint32_t new_name_len);
bool directory_is_empty(ext2_volume_t *volume, uint32_t inode_number);
bool update_directory_parent_link(ext2_volume_t *volume, uint32_t dir_inode_number, uint32_t parent_inode_number);
bool remove_directory_entry(ext2_volume_t *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len);
bool setup_new_file_inode(ext2_inode_t *inode);
bool setup_new_dir_inode(ext2_inode_t *inode, ext2_volume_t *volume);
bool initialize_directory_block(ext2_volume_t *volume, uint32_t inode_number, uint32_t parent_inode_number, uint32_t block_number);
bool lookup_parent_and_name(ext2_volume_t *volume, const char *path, uint32_t *parent_inode_out, char *name_out, uint32_t name_out_size, char *parent_path, uint32_t parent_path_size);
bool lookup_child_type(ext2_volume_t *volume, uint32_t parent_inode_number, const char *name, uint32_t name_len, uint32_t *child_inode_out, uint8_t *child_type_out);
bool free_inode_and_blocks(ext2_volume_t *volume, uint32_t inode_number, ext2_inode_t *inode);
