#include "ext2_internal.h"

/**
 * reads the next directory entry from an open directory file, populating the provided ext2_directory_entry_t structure with the entry's details if successful.
 * @param file pointer to the ext2_file_t structure representing the open directory file.
 * @param entryOut pointer to the ext2_directory_entry_t structure where the read entry's details will be stored.
 * @return true if a directory entry was successfully read and stored in entryOut, false if the end of the directory was reached, the file is not a directory, or an error occurred during the read operation.
 */
static bool read_directory_entry(ext2_file_t *file, ext2_directory_entry_t *entryOut)
{
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;
    uint32_t block_size;

    if (!file || !entryOut || !file->volume || !file->is_open || !inode_is_directory(&file->inode_cache))
    {
        return false;
    }

    block_size = file->volume->block_size;

    while (file->cursor < file->inode_cache.i_size)
    {
        uint32_t block_index = file->cursor / block_size;
        uint32_t block_offset = file->cursor % block_size;
        uint32_t physical_block;
        ext2_dir_entry_t *entry;

        if (!resolve_data_block(file->volume, &file->inode_cache, block_index, &physical_block))
        {
            return false;
        }

        if (!read_block(file->volume, physical_block, g_block_buffer))
        {
            return false;
        }

        if (block_offset + header_size > block_size)
        {
            file->cursor = (block_index + 1u) * block_size;
            continue;
        }

        entry = (ext2_dir_entry_t *)(g_block_buffer + block_offset);
        if (entry->rec_len == 0 || block_offset + entry->rec_len > block_size)
        {
            return false;
        }

        if (entry->rec_len < header_size)
        {
            return false;
        }

        if ((uint32_t)entry->name_len > (entry->rec_len - header_size))
        {
            return false;
        }

        file->cursor += entry->rec_len;

        if (entry->inode == 0 || entry->name_len == 0)
        {
            continue;
        }

        entryOut->inode = entry->inode;
        entryOut->file_type = entry->file_type;
        entryOut->size = 0;

        memcpy(entryOut->name, g_block_buffer + block_offset + header_size, entry->name_len);
        entryOut->name[entry->name_len] = 0;
        return true;
    }

    return false;
}

kerr_t ext2_open(ext2_volume_t *volume, const char *path, ext2_file_t *file)
{
    uint32_t inode_number;
    ext2_inode_t inode;

    if (!volume || !path || !file)
    {
        return KERR_INVAL;
    }

    if (kerr_failed(ext2_lookup_path(volume, path, &inode_number)))
    {
        return KERR_NOTFOUND;
    }

    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)))
    {
        return KERR_IO;
    }

    file->inode = inode_number;
    file->size = inode.i_size;
    file->cursor = 0;
    file->file_type = inode_to_file_type(&inode);
    file->is_open = 1;
    file->volume = volume;
    file->inode_cache = inode;
    return KERR_OK;
}

uint32_t ext2_read(ext2_file_t *file, uint32_t byteCount, void *dataOut)
{
    uint32_t available;

    if (!file || !file->is_open || !file->volume || !dataOut)
    {
        return 0;
    }

    if (!inode_is_regular(&file->inode_cache))
    {
        return 0;
    }

    if (file->cursor >= file->size)
    {
        return 0;
    }

    available = file->size - file->cursor;
    if (byteCount > available)
    {
        byteCount = available;
    }

    if (byteCount == 0)
    {
        return 0;
    }

    if (kerr_failed(ext2_read_file(file->volume, file->inode, file->cursor, byteCount, dataOut)))
    {
        return 0;
    }

    file->cursor += byteCount;
    return byteCount;
}

uint32_t ext2_write(ext2_file_t *file, uint32_t byteCount, const void *dataIn)
{
    if (!file || !file->is_open || !file->volume || !dataIn)
    {
        return 0;
    }

    if (!inode_is_regular(&file->inode_cache))
    {
        return 0;
    }

    if (byteCount == 0)
    {
        return 0;
    }

    if (kerr_failed(ext2_write_file(file->volume, file->inode, file->cursor, byteCount, dataIn)))
    {
        return 0;
    }

    file->cursor += byteCount;

    /* update cached size if we extended the file */
    if (file->cursor > file->size)
    {
        file->size = file->cursor;
    }

    return byteCount;
}

void ext2_truncate(ext2_file_t *file)
{
    if (!file || !file->is_open || !file->volume)
        return;

    (void)ext2_truncate_file(file->volume, file->inode);
    file->size = 0;
    file->cursor = 0;
    file->inode_cache.i_size = 0;
}

kerr_t ext2_read_entry(ext2_file_t *file, ext2_directory_entry_t *entryOut)
{
    if (!file || !entryOut)
    {
        return KERR_INVAL;
    }

    if (!file->is_open)
    {
        return KERR_INVAL;
    }

    if (!inode_is_directory(&file->inode_cache))
    {
        return KERR_NOTDIR;
    }

    if (!read_directory_entry(file, entryOut))
    {
        return KERR_NOENT;
    }

    return KERR_OK;
}

void ext2_close(ext2_file_t *file)
{
    if (!file)
    {
        return;
    }

    file->inode = 0;
    file->size = 0;
    file->cursor = 0;
    file->file_type = 0;
    file->is_open = 0;
    file->volume = 0;
    memset(&file->inode_cache, 0, (uint32_t)sizeof(ext2_inode_t));
}

kerr_t ext2_read_file(ext2_volume_t *volume, uint32_t inode_number, uint32_t offset, uint32_t length, void *buffer)
{
    ext2_inode_t inode;
    uint8_t *out = (uint8_t *)buffer;
    uint32_t copied = 0;

    if (!volume || !buffer)
    {
        return KERR_INVAL;
    }

    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)) || !inode_is_regular(&inode))
    {
        return KERR_IO;
    }

    if (length == 0)
    {
        return KERR_OK;
    }

    if (offset >= inode.i_size)
    {
        return KERR_RANGE;
    }

    if (offset + length < offset)
    {
        return KERR_INVAL;
    }

    if (offset + length > inode.i_size)
    {
        length = inode.i_size - offset;
    }

    while (copied < length)
    {
        uint32_t file_pos = offset + copied;
        uint32_t block_index = file_pos / volume->block_size;
        uint32_t block_offset = file_pos % volume->block_size;
        uint32_t take = EXT2_MIN(length - copied, volume->block_size - block_offset);
        uint32_t phys_block;
        uint32_t j;

        if (!resolve_data_block(volume, &inode, block_index, &phys_block))
        {
            return KERR_IO;
        }

        if (phys_block == 0)
        {
            for (j = 0; j < take; j++)
            {
                out[copied + j] = 0;
            }

            copied += take;
            continue;
        }

        if (!read_block(volume, phys_block, g_block_buffer))
        {
            return KERR_IO;
        }

        for (j = 0; j < take; j++)
        {
            out[copied + j] = g_block_buffer[block_offset + j];
        }

        copied += take;
    }

    return KERR_OK;
}

kerr_t ext2_create_file(ext2_volume_t *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    ext2_inode_t parent_inode;
    ext2_inode_t new_inode;
    uint32_t child_inode_number;
    uint32_t name_len;
    uint8_t child_type;

    if (!volume || !path)
    {
        return KERR_INVAL;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return KERR_NOTFOUND;
    }

    name_len = (uint32_t)strlen(name);
    if (lookup_child_type(volume, parent_inode_number, name, name_len, 0, &child_type))
    {
        return KERR_EXIST;
    }

    if (kerr_failed(ext2_read_inode(volume, parent_inode_number, &parent_inode)) || !inode_is_directory(&parent_inode))
    {
        return KERR_IO;
    }

    if (!alloc_inode(volume, &child_inode_number))
    {
        return KERR_NOSPC;
    }

    if (!setup_new_file_inode(&new_inode))
    {
        return KERR_IO;
    }

    if (!write_inode(volume, child_inode_number, &new_inode))
    {
        free_inode(volume, child_inode_number);
        return KERR_IO;
    }

    if (!append_or_replace_directory_entry(volume, parent_inode_number, &parent_inode, name, name_len, child_inode_number, EXT2_FT_REG_FILE))
    {
        free_inode(volume, child_inode_number);
        return KERR_NOSPC;
    }

    parent_inode.i_mtime = 0;
    parent_inode.i_ctime = 0;
    write_inode(volume, parent_inode_number, &parent_inode);
    return KERR_OK;
}

kerr_t ext2_create_dir(ext2_volume_t *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    ext2_inode_t parent_inode;
    ext2_inode_t new_inode;
    uint32_t child_inode_number;
    uint32_t child_block_number;
    uint32_t name_len;
    uint32_t child_group;

    if (!volume || !path)
    {
        return KERR_INVAL;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return KERR_NOTFOUND;
    }

    name_len = (uint32_t)strlen(name);
    if (lookup_child_type(volume, parent_inode_number, name, name_len, 0, 0))
    {
        return KERR_EXIST;
    }

    if (kerr_failed(ext2_read_inode(volume, parent_inode_number, &parent_inode)) || !inode_is_directory(&parent_inode))
    {
        return KERR_IO;
    }

    if (!alloc_inode(volume, &child_inode_number))
    {
        return KERR_NOSPC;
    }

    child_group = (child_inode_number - 1u) / volume->inodes_per_group;

    if (!alloc_block(volume, &child_block_number))
    {
        free_inode(volume, child_inode_number);
        return KERR_NOSPC;
    }

    if (!setup_new_dir_inode(&new_inode, volume))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return KERR_IO;
    }

    new_inode.i_block[0] = child_block_number;
    if (!initialize_directory_block(volume, child_inode_number, parent_inode_number, child_block_number))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return KERR_IO;
    }

    if (!write_inode(volume, child_inode_number, &new_inode))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return KERR_IO;
    }

    if (!append_or_replace_directory_entry(volume, parent_inode_number, &parent_inode, name, name_len, child_inode_number, EXT2_FT_DIR))
    {
        free_block(volume, child_block_number);
        free_inode(volume, child_inode_number);
        return KERR_NOSPC;
    }

    parent_inode.i_links_count++;
    parent_inode.i_mtime = 0;
    parent_inode.i_ctime = 0;
    if (!write_inode(volume, parent_inode_number, &parent_inode))
    {
        return KERR_IO;
    }

    update_group_and_super_counts(volume, child_group, 0, 0, 1);
    return KERR_OK;
}

kerr_t ext2_remove_file(ext2_volume_t *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    uint32_t name_len;

    if (!volume || !path)
    {
        return KERR_INVAL;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return KERR_NOTFOUND;
    }

    name_len = (uint32_t)strlen(name);
    if (!lookup_child_type(volume, parent_inode_number, name, name_len, &child_inode_number, &child_type) || child_type != EXT2_FT_REG_FILE)
    {
        return KERR_NOTFOUND;
    }

    if (!remove_directory_entry(volume, parent_inode_number, name, name_len))
    {
        return KERR_IO;
    }

    if (!free_inode_and_blocks(volume, child_inode_number, 0))
    {
        return KERR_IO;
    }

    return KERR_OK;
}

kerr_t ext2_remove_dir(ext2_volume_t *volume, const char *path)
{
    char parent_path[256];
    char name[256];
    uint32_t parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    ext2_inode_t child_inode;
    ext2_inode_t parent_inode;
    uint32_t name_len;
    uint32_t child_group;

    if (!volume || !path)
    {
        return KERR_INVAL;
    }

    if (!lookup_parent_and_name(volume, path, &parent_inode_number, name, sizeof(name), parent_path, sizeof(parent_path)))
    {
        return KERR_NOTFOUND;
    }

    name_len = (uint32_t)strlen(name);
    if (!lookup_child_type(volume, parent_inode_number, name, name_len, &child_inode_number, &child_type) || child_type != EXT2_FT_DIR)
    {
        return KERR_NOTFOUND;
    }

    if (kerr_failed(ext2_read_inode(volume, child_inode_number, &child_inode)) || !inode_is_directory(&child_inode))
    {
        return KERR_IO;
    }

    if (!directory_is_empty(volume, child_inode_number))
    {
        return KERR_BUSY;
    }

    if (!remove_directory_entry(volume, parent_inode_number, name, name_len))
    {
        return KERR_IO;
    }

    if (kerr_failed(ext2_read_inode(volume, parent_inode_number, &parent_inode)))
    {
        return KERR_IO;
    }

    if (parent_inode.i_links_count > 0)
    {
        parent_inode.i_links_count--;
        parent_inode.i_mtime = 0;
        parent_inode.i_ctime = 0;
        write_inode(volume, parent_inode_number, &parent_inode);
    }

    child_group = (child_inode_number - 1u) / volume->inodes_per_group;
    if (!free_inode_and_blocks(volume, child_inode_number, &child_inode))
    {
        return KERR_IO;
    }

    update_group_and_super_counts(volume, child_group, 0, 0, -1);
    return KERR_OK;
}

kerr_t ext2_rename(ext2_volume_t *volume, const char *old_path, const char *new_path)
{
    char old_parent_path[256];
    char old_name[256];
    char new_parent_path[256];
    char new_name[256];
    uint32_t old_parent_inode_number;
    uint32_t new_parent_inode_number;
    uint32_t child_inode_number;
    uint8_t child_type;
    ext2_inode_t old_parent_inode;
    ext2_inode_t new_parent_inode;
    uint32_t old_name_len;
    uint32_t new_name_len;
    uint32_t child_group;

    if (!volume || !old_path || !new_path)
    {
        return KERR_INVAL;
    }

    if (!lookup_parent_and_name(volume, old_path, &old_parent_inode_number, old_name, sizeof(old_name), old_parent_path, sizeof(old_parent_path)))
    {
        return KERR_NOTFOUND;
    }

    if (!lookup_parent_and_name(volume, new_path, &new_parent_inode_number, new_name, sizeof(new_name), new_parent_path, sizeof(new_parent_path)))
    {
        return KERR_NOTFOUND;
    }

    old_name_len = (uint32_t)strlen(old_name);
    new_name_len = (uint32_t)strlen(new_name);

    if (!lookup_child_type(volume, old_parent_inode_number, old_name, old_name_len, &child_inode_number, &child_type))
    {
        return KERR_NOTFOUND;
    }

    if (kerr_failed(ext2_read_inode(volume, old_parent_inode_number, &old_parent_inode)) || !inode_is_directory(&old_parent_inode))
    {
        return KERR_IO;
    }

    if (kerr_failed(ext2_read_inode(volume, new_parent_inode_number, &new_parent_inode)) || !inode_is_directory(&new_parent_inode))
    {
        return KERR_IO;
    }

    if (lookup_child_type(volume, new_parent_inode_number, new_name, new_name_len, 0, 0))
    {
        return KERR_EXIST;
    }

    if (old_parent_inode_number == new_parent_inode_number)
    {
        if (update_directory_entry_name(volume, old_parent_inode_number, old_name, old_name_len, new_name, new_name_len))
        {
            return KERR_OK;
        }
    }

    if (!append_or_replace_directory_entry(volume, new_parent_inode_number, &new_parent_inode, new_name, new_name_len, child_inode_number, child_type))
    {
        return KERR_NOSPC;
    }

    if (child_type == EXT2_FT_DIR && old_parent_inode_number != new_parent_inode_number)
    {
        if (!update_directory_parent_link(volume, child_inode_number, new_parent_inode_number))
        {
            remove_directory_entry(volume, new_parent_inode_number, new_name, new_name_len);
            return KERR_IO;
        }

        child_group = (child_inode_number - 1u) / volume->inodes_per_group;
        if (old_parent_inode.i_links_count > 0)
        {
            old_parent_inode.i_links_count--;
        }

        new_parent_inode.i_links_count++;
        write_inode(volume, old_parent_inode_number, &old_parent_inode);
        write_inode(volume, new_parent_inode_number, &new_parent_inode);
        update_group_and_super_counts(volume, child_group, 0, 0, 0);
    }

    if (!remove_directory_entry(volume, old_parent_inode_number, old_name, old_name_len))
    {
        return KERR_IO;
    }

    return KERR_OK;
}

/**
 * assign a physical block to a logical block index in an inode.
 * if the logical block is not yet allocated (i_block entry is 0), a new
 * block is allocated. only supports direct blocks (indices 0-11).
 *
 * @param volume ext2 volume.
 * @param inode pointer to the inode (modified in place).
 * @param logical_block_index the logical block index.
 * @param physical_block_out receives the physical block number.
 * @return true on success, false on failure.
 */
static bool assign_data_block(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    /* only direct blocks for now */
    if (logical_block_index >= EXT2_NDIR_BLOCKS)
    {
        return false;
    }

    if (inode->i_block[logical_block_index] != 0)
    {
        /* already allocated */
        *physical_block_out = inode->i_block[logical_block_index];
        return true;
    }

    /* allocate a new block */
    uint32_t new_block;
    if (!alloc_block(volume, &new_block))
    {
        return false;
    }

    /* zero the new block on disk */
    memset(g_block_buffer, 0, volume->block_size);
    if (!write_block(volume, new_block, g_block_buffer))
    {
        return false;
    }

    inode->i_block[logical_block_index] = new_block;
    inode->i_blocks += volume->block_size / 512; /* ext2 counts in 512-byte units */

    *physical_block_out = new_block;
    return true;
}

kerr_t ext2_write_file(ext2_volume_t *volume, uint32_t inode_number, uint32_t offset, uint32_t length, const void *buffer)
{
    ext2_inode_t inode;
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t written = 0;

    if (!volume || !buffer)
    {
        return KERR_INVAL;
    }

    if (length == 0)
    {
        return KERR_OK;
    }

    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)) || !inode_is_regular(&inode))
    {
        return KERR_IO;
    }

    while (written < length)
    {
        uint32_t file_pos = offset + written;
        uint32_t block_index = file_pos / volume->block_size;
        uint32_t block_offset = file_pos % volume->block_size;
        uint32_t chunk = length - written;
        uint32_t phys_block;

        if (chunk > volume->block_size - block_offset)
            chunk = volume->block_size - block_offset;

        /* ensure a physical block is allocated for this logical index */
        if (!assign_data_block(volume, &inode, block_index, &phys_block))
        {
            return KERR_NOSPC;
        }

        /* read the existing block (for partial block writes) */
        if (!read_block(volume, phys_block, g_block_buffer))
        {
            return KERR_IO;
        }

        /* copy user data into the block buffer */
        memcpy(g_block_buffer + block_offset, src + written, chunk);

        /* write the block back */
        if (!write_block(volume, phys_block, g_block_buffer))
        {
            return KERR_IO;
        }

        written += chunk;
    }

    /* update inode size if we extended the file */
    uint32_t new_end = offset + length;
    if (new_end > inode.i_size)
    {
        inode.i_size = new_end;
    }

    /* write back the updated inode */
    if (!write_inode(volume, inode_number, &inode))
    {
        return KERR_IO;
    }

    return KERR_OK;
}

kerr_t ext2_truncate_file(ext2_volume_t *volume, uint32_t inode_number)
{
    ext2_inode_t inode;

    if (!volume)
        return KERR_INVAL;

    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)) || !inode_is_regular(&inode))
        return KERR_IO;

    /* just set size to 0  blocks remain allocated (simple approach) */
    inode.i_size = 0;

    if (!write_inode(volume, inode_number, &inode))
        return KERR_IO;

    return KERR_OK;
}
