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

        if (!read_block(file->volume, physical_block, file->volume->buf_block))
        {
            return false;
        }

        if (block_offset + header_size > block_size)
        {
            file->cursor = (block_index + 1u) * block_size;
            continue;
        }

        entry = (ext2_dir_entry_t *)(file->volume->buf_block + block_offset);
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

        memcpy(entryOut->name, file->volume->buf_block + block_offset + header_size, entry->name_len);
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

kerr_t ext2_truncate(ext2_file_t *file, uint32_t length)
{
    if (!file || !file->is_open || !file->volume)
        return KERR_INVAL;

    kerr_t err = ext2_truncate_file(file->volume, file->inode, length);
    if (kerr_failed(err))
        return err;
    // the cursor stays where it is, as ftruncate asks
    file->size = length;
    return ext2_read_inode(file->volume, file->inode, &file->inode_cache);
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

        if (!read_block(volume, phys_block, volume->buf_block))
        {
            return KERR_IO;
        }

        for (j = 0; j < take; j++)
        {
            out[copied + j] = volume->buf_block[block_offset + j];
        }

        copied += take;
    }

    return KERR_OK;
}

kerr_t ext2_create_file(ext2_volume_t *volume, const char *path)
{
    char parent_path[EXT2_NAME_MAX];
    char name[EXT2_NAME_MAX];
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

    parent_inode.i_mtime = ext2_now();
    parent_inode.i_ctime = parent_inode.i_mtime;
    write_inode(volume, parent_inode_number, &parent_inode);
    return KERR_OK;
}

kerr_t ext2_create_dir(ext2_volume_t *volume, const char *path)
{
    char parent_path[EXT2_NAME_MAX];
    char name[EXT2_NAME_MAX];
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
    parent_inode.i_mtime = ext2_now();
    parent_inode.i_ctime = parent_inode.i_mtime;
    if (!write_inode(volume, parent_inode_number, &parent_inode))
    {
        return KERR_IO;
    }

    update_group_and_super_counts(volume, child_group, 0, 0, 1);
    return KERR_OK;
}

kerr_t ext2_remove_file(ext2_volume_t *volume, const char *path)
{
    char parent_path[EXT2_NAME_MAX];
    char name[EXT2_NAME_MAX];
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
    if (!lookup_child_type(volume, parent_inode_number, name, name_len, &child_inode_number, &child_type))
    {
        return KERR_NOTFOUND;
    }
    if (child_type == EXT2_FT_DIR)
    {
        return KERR_ISDIR;
    }

    ext2_inode_t child;
    if (kerr_failed(ext2_read_inode(volume, child_inode_number, &child)))
    {
        return KERR_IO;
    }

    if (!remove_directory_entry(volume, parent_inode_number, name, name_len))
    {
        return KERR_IO;
    }

    // the data stays while the file has other names (hard links)
    if (child.i_links_count > 1)
    {
        child.i_links_count--;
        child.i_ctime = ext2_now();
        return write_inode(volume, child_inode_number, &child) ? KERR_OK : KERR_IO;
    }

    if (!free_inode_and_blocks(volume, child_inode_number, &child))
    {
        return KERR_IO;
    }

    return KERR_OK;
}

kerr_t ext2_remove_dir(ext2_volume_t *volume, const char *path)
{
    char parent_path[EXT2_NAME_MAX];
    char name[EXT2_NAME_MAX];
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
        return KERR_NOTEMPTY;
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
        parent_inode.i_mtime = ext2_now();
        parent_inode.i_ctime = parent_inode.i_mtime;
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
    char old_parent_path[EXT2_NAME_MAX];
    char old_name[EXT2_NAME_MAX];
    char new_parent_path[EXT2_NAME_MAX];
    char new_name[EXT2_NAME_MAX];
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

    // a directory cannot move into itself or below itself
    size_t old_path_len = strlen(old_path);
    if (child_type == EXT2_FT_DIR && strncmp(new_path, old_path, old_path_len) == 0 &&
        (new_path[old_path_len] == '/' || new_path[old_path_len] == '\0'))
    {
        return new_path[old_path_len] == '\0' ? KERR_OK : KERR_INVAL;
    }

    // an existing target is replaced: a file by a file, an empty directory
    // by a directory
    uint32_t target_inode_number;
    uint8_t target_type;
    if (lookup_child_type(volume, new_parent_inode_number, new_name, new_name_len, &target_inode_number, &target_type))
    {
        kerr_t err;
        if (target_inode_number == child_inode_number)
        {
            return KERR_OK; // two names of the same file
        }
        if (child_type == EXT2_FT_DIR)
        {
            if (target_type != EXT2_FT_DIR)
            {
                return KERR_NOTDIR;
            }
            err = ext2_remove_dir(volume, new_path);
        }
        else
        {
            if (target_type == EXT2_FT_DIR)
            {
                return KERR_ISDIR;
            }
            err = ext2_remove_file(volume, new_path);
        }
        if (kerr_failed(err))
        {
            return err;
        }
    }

    // read after the replace step: removing a directory changes its parent
    if (kerr_failed(ext2_read_inode(volume, old_parent_inode_number, &old_parent_inode)) || !inode_is_directory(&old_parent_inode))
    {
        return KERR_IO;
    }

    if (kerr_failed(ext2_read_inode(volume, new_parent_inode_number, &new_parent_inode)) || !inode_is_directory(&new_parent_inode))
    {
        return KERR_IO;
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
 * allocate a block, zero it on disk and count it in the inode.
 * uses volume->buf_block, so a pointer table must be in another buffer.
 */
static bool alloc_zeroed_block(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t *block_out)
{
    if (!alloc_block(volume, block_out))
    {
        return false;
    }

    memset(volume->buf_block, 0, volume->block_size);
    if (!write_block(volume, *block_out, volume->buf_block))
    {
        return false;
    }

    inode->i_blocks += volume->block_size / EXT2_SECTOR_SIZE;
    return true;
}

/**
 * the block that entry index of a pointer table points to, allocated (and
 * stored in the table) if it is not there yet.
 * @param table_block the pointer table.
 * @param index the entry.
 * @param buf a block buffer for the table (not volume->buf_block).
 */
static bool assign_in_table(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t table_block,
                            uint32_t index, uint8_t *buf, uint32_t *block_out)
{
    if (!read_block(volume, table_block, buf))
    {
        return false;
    }

    uint32_t *entries = (uint32_t *)buf;
    if (entries[index] == 0)
    {
        uint32_t new_block;
        if (!alloc_zeroed_block(volume, inode, &new_block))
        {
            return false;
        }
        entries[index] = new_block;
        if (!write_block(volume, table_block, buf))
        {
            return false;
        }
    }

    *block_out = entries[index];
    return true;
}

/**
 * make sure the inode has its single (slot 12) or double (slot 13)
 * indirect table.
 */
static bool ensure_table(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t slot)
{
    if (inode->i_block[slot] != 0)
    {
        return true;
    }
    uint32_t block;
    if (!alloc_zeroed_block(volume, inode, &block))
    {
        return false;
    }
    inode->i_block[slot] = block;
    return true;
}

/**
 * the physical block of a file's logical block, allocated if needed:
 * through the 12 direct pointers, the single-indirect table and the
 * double-indirect tables (files up to 64 MiB with 1 KiB blocks). the
 * caller writes the inode back afterwards.
 * @return false if the volume is full, the file would need the
 *         triple-indirect tables, or I/O failed.
 */
static bool assign_data_block(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    uint32_t pointers = volume->block_size / sizeof(uint32_t);
    uint32_t index = logical_block_index;

    if (index < EXT2_NDIR_BLOCKS)
    {
        if (inode->i_block[index] == 0)
        {
            // the inode is packed: allocate into a local, then store
            uint32_t block;
            if (!alloc_zeroed_block(volume, inode, &block))
            {
                return false;
            }
            inode->i_block[index] = block;
        }
        *physical_block_out = inode->i_block[index];
        return true;
    }

    index -= EXT2_NDIR_BLOCKS;
    if (index < pointers)
    {
        if (!ensure_table(volume, inode, 12))
        {
            return false;
        }
        return assign_in_table(volume, inode, inode->i_block[12], index, volume->buf_block2,
                               physical_block_out);
    }

    index -= pointers;
    if (index < pointers * pointers)
    {
        if (!ensure_table(volume, inode, 13))
        {
            return false;
        }
        uint32_t table;
        if (!assign_in_table(volume, inode, inode->i_block[13], index / pointers,
                             volume->buf_block3, &table))
        {
            return false;
        }
        return assign_in_table(volume, inode, table, index % pointers, volume->buf_block2,
                               physical_block_out);
    }

    return false; // would need the triple-indirect tables
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
        if (!read_block(volume, phys_block, volume->buf_block))
        {
            return KERR_IO;
        }

        /* copy user data into the block buffer */
        memcpy(volume->buf_block + block_offset, src + written, chunk);

        /* write the block back */
        if (!write_block(volume, phys_block, volume->buf_block))
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
    inode.i_mtime = ext2_now();
    inode.i_ctime = inode.i_mtime;

    /* write back the updated inode */
    if (!write_inode(volume, inode_number, &inode))
    {
        return KERR_IO;
    }

    return KERR_OK;
}

/**
 * the volume buffer for a pointer table at a level (1: its entries are data
 * blocks). each level of a walk needs its own: the tables above stay read.
 */
static uint32_t *table_buffer(ext2_volume_t *volume, uint32_t level)
{
    if (level == 1)
        return (uint32_t *)volume->buf_block;
    return (uint32_t *)(level == 2 ? volume->buf_block3 : volume->buf_block4);
}

/**
 * free the blocks below a pointer table whose logical index is first or
 * later.
 * @param level 1: the entries are data blocks; 2 and 3: tables.
 * @param base the logical index of the table's first block.
 * @param freed counts the blocks given back (data and tables).
 * @param emptied_out set when the table has no entries left (the caller
 *        then frees the table itself).
 */
static bool trim_table(ext2_volume_t *volume, uint32_t table, uint32_t level, uint32_t base,
                       uint32_t first, uint32_t *freed, bool *emptied_out)
{
    uint32_t per_block = volume->block_size / sizeof(uint32_t);
    uint32_t span = 1; // the blocks one entry covers
    for (uint32_t l = 1; l < level; l++)
    {
        span *= per_block;
    }

    uint32_t *entries = table_buffer(volume, level);
    if (!read_block(volume, table, entries))
        return false;

    bool changed = false;
    bool left = false;
    for (uint32_t j = 0; j < per_block; j++)
    {
        if (entries[j] == 0)
        {
            continue;
        }
        uint32_t start = base + j * span;
        if (start + span <= first)
        {
            left = true; // wholly before the new end
            continue;
        }
        bool gone = true;
        if (level > 1)
        {
            if (!trim_table(volume, entries[j], level - 1, start, first, freed, &gone))
                return false;
        }
        else
        {
            gone = start >= first;
        }
        if (gone)
        {
            if (!free_block(volume, entries[j]))
                return false;
            entries[j] = 0;
            (*freed)++;
            changed = true;
        }
        else
        {
            left = true;
        }
    }

    if (changed && left && !write_block(volume, table, entries))
        return false;
    *emptied_out = !left;
    return true;
}

/**
 * free the blocks of an inode from logical index first on (data and the
 * pointer tables that become empty).
 * @param freed counts the blocks given back.
 */
static bool free_blocks_from(ext2_volume_t *volume, ext2_inode_t *inode, uint32_t first,
                             uint32_t *freed)
{
    uint32_t per_block = volume->block_size / sizeof(uint32_t);
    for (uint32_t i = first; i < EXT2_NDIR_BLOCKS; i++)
    {
        if (inode->i_block[i] == 0)
        {
            continue;
        }
        if (!free_block(volume, inode->i_block[i]))
            return false;
        inode->i_block[i] = 0;
        (*freed)++;
    }

    // the single, double and triple indirect tables, and where each starts
    uint32_t base = EXT2_NDIR_BLOCKS;
    uint32_t span = per_block;
    for (uint32_t level = 1; level <= 3; level++)
    {
        uint32_t slot = EXT2_NDIR_BLOCKS + level - 1; // i_block is packed: no pointer
        uint32_t table = inode->i_block[slot];
        if (table != 0 && base + span > first)
        {
            bool emptied;
            if (!trim_table(volume, table, level, base, first, freed, &emptied))
                return false;
            if (emptied)
            {
                if (!free_block(volume, table))
                    return false;
                inode->i_block[slot] = 0;
                (*freed)++;
            }
        }
        base += span;
        span *= per_block;
    }
    return true;
}

/**
 * zero the bytes of the last block past the new end, so that a later
 * growth reads zeros there.
 */
static bool zero_tail(ext2_volume_t *volume, const ext2_inode_t *inode, uint32_t length)
{
    uint32_t used = length % volume->block_size;
    uint32_t block;
    if (used == 0)
        return true;
    if (!resolve_data_block(volume, inode, length / volume->block_size, &block))
        return false;
    if (block == 0)
        return true; // a hole
    if (!read_block(volume, block, volume->buf_block))
        return false;
    memset(volume->buf_block + used, 0, volume->block_size - used);
    return write_block(volume, block, volume->buf_block);
}

kerr_t ext2_truncate_file(ext2_volume_t *volume, uint32_t inode_number, uint32_t length)
{
    ext2_inode_t inode;

    if (!volume)
        return KERR_INVAL;

    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)) || !inode_is_regular(&inode))
        return KERR_IO;

    if (length == 0)
    {
        // give every data block and pointer table back to the volume
        if (!free_inode_block_chain(volume, &inode))
            return KERR_IO;
        inode.i_blocks = 0;
    }
    else if (length < inode.i_size)
    {
        uint32_t first = (length + volume->block_size - 1) / volume->block_size;
        uint32_t freed = 0;
        bool ok = free_blocks_from(volume, &inode, first, &freed);
        inode.i_blocks -= freed * volume->sectors_per_block;
        if (!ok || !zero_tail(volume, &inode, length))
        {
            write_inode(volume, inode_number, &inode);
            return KERR_IO;
        }
    }
    // a longer file gets a hole: the new bytes read as zeros
    inode.i_size = length;
    inode.i_mtime = ext2_now();
    inode.i_ctime = inode.i_mtime;

    if (!write_inode(volume, inode_number, &inode))
        return KERR_IO;

    return KERR_OK;
}
