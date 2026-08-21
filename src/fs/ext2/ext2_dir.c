#include "ext2_internal.h"

/**
 * splits a given path into its parent directory and the name of the final component (file or directory).
 * @param path the input path string to be split (must start with '/').
 * @param parent buffer to store the parent directory path.
 * @param parent_size the size of the parent buffer in bytes.
 * @param name buffer to store the name of the final component.
 * @param name_size the size of the name buffer in bytes.
 * @return true if the path was successfully split into parent and name, false if the path was invalid or the buffers were too small to hold the results.
 */
static bool split_path(const char *path, char *parent, uint32_t parent_size, char *name, uint32_t name_size)
{
    uint32_t len;
    uint32_t end;
    uint32_t start;
    uint32_t parent_len;
    uint32_t i;

    if (!path || !parent || !name || parent_size == 0 || name_size == 0 || path[0] != '/')
    {
        return false;
    }

    len = (uint32_t)strlen(path);
    while (len > 1u && path[len - 1u] == '/')
    {
        len--;
    }

    if (len <= 1u)
    {
        return false;
    }

    end = len;
    start = end;
    while (start > 0u && path[start - 1u] != '/')
    {
        start--;
    }

    if (start == 0u)
    {
        return false;
    }

    if ((end - start) + 1u > name_size)
    {
        return false;
    }

    for (i = 0; i < end - start; i++)
    {
        name[i] = path[start + i];
    }
    name[end - start] = '\0';

    parent_len = start - 1u;
    if (parent_len == 0u)
    {
        if (parent_size < 2u)
        {
            return false;
        }

        parent[0] = '/';
        parent[1] = '\0';
        return true;
    }

    if (parent_len + 1u > parent_size)
    {
        return false;
    }

    for (i = 0; i < parent_len; i++)
    {
        parent[i] = path[i];
    }
    parent[parent_len] = '\0';
    return true;
}

/**
 * searches for a directory entry with a specific name within a given directory inode, returning the block index, offset, and entry details if found.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory to search within.
 * @param name the name of the directory entry to search for.
 * @param name_len the length of the name to search for.
 * @param block_index_out pointer to a variable where the block index of the found entry will be stored.
 * @param offset_out pointer to a variable where the byte offset of the found entry within the block will be stored.
 * @param entry_out pointer to an ext2_dir_entry_t structure where the details of the found entry will be stored.
 * @return true if the directory entry was found and its details are stored in the output parameters, false if the entry was not found or an error occurred during the search.
 */
bool find_directory_entry(ext2_volume_t *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *block_index_out, uint32_t *offset_out, ext2_dir_entry_t *entry_out)
{
    ext2_inode_t dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;

    if (!volume || !name || !block_index_out || !offset_out || !entry_out)
    {
        return false;
    }

    if (kerr_failed(ext2_read_inode(volume, dir_inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = dir_inode.i_block[block_index];
        uint32_t offset = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (offset + header_size <= volume->block_size)
        {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;
            const char *entry_name;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return false;
            }

            entry_name = (const char *)(g_block_buffer + offset + header_size);
            if (entry->inode != 0 && name_len == entry->name_len && memcmp(name, entry_name, name_len) == 0)
            {
                *block_index_out = block_index;
                *offset_out = offset;
                *entry_out = *entry;
                return true;
            }

            offset += entry->rec_len;
        }
    }

    return false;
}

/**
 * appends a new directory entry to a parent directory inode or replaces an existing entry with the same name, ensuring proper space allocation and alignment within the directory's data blocks.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param parent_inode_number the inode number of the parent directory where the entry will be added or replaced.
 * @param parent_inode pointer to the ext2_inode_t structure representing the parent directory inode.
 * @param name the name of the directory entry to add or replace.
 * @param name_len the length of the name of the directory entry.
 * @param child_inode_number the inode number of the child entry to be added or replaced.
 * @param file_type the type of the file (e.g., regular file, directory) for the new directory entry.
 * @return true if the directory entry was successfully appended or replaced,
 */
bool append_or_replace_directory_entry(ext2_volume_t *volume, uint32_t parent_inode_number, ext2_inode_t *parent_inode, const char *name, uint32_t name_len, uint32_t child_inode_number, uint8_t file_type)
{
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;
    uint32_t needed = ext2_dir_entry_size(name_len);
    uint32_t block_index;
    if (!volume || !parent_inode || !name || name_len == 0 || name_len >= 255)
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = parent_inode->i_block[block_index];
        uint32_t cursor = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (cursor + header_size <= volume->block_size)
        {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(g_block_buffer + cursor);
            uint32_t used = ext2_dir_entry_size(entry->name_len);
            uint32_t available;

            if (entry->rec_len == 0 || cursor + entry->rec_len > volume->block_size)
            {
                return false;
            }

            available = entry->rec_len;
            if (entry->inode == 0)
            {
                if (available >= needed)
                {
                    ext2_dir_entry_t *new_entry = entry;

                    memset(new_entry, 0, available);
                    new_entry->inode = child_inode_number;
                    new_entry->rec_len = (uint16_t)available;
                    new_entry->name_len = (uint8_t)name_len;
                    new_entry->file_type = file_type;
                    memcpy((uint8_t *)new_entry + header_size, name, name_len);
                    return write_block(volume, data_block, g_block_buffer);
                }
            }
            else if (available >= used + needed)
            {
                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(g_block_buffer + cursor + used);
                uint32_t remaining = available - used;

                entry->rec_len = (uint16_t)used;
                memset(new_entry, 0, remaining);
                new_entry->inode = child_inode_number;
                new_entry->rec_len = (uint16_t)remaining;
                new_entry->name_len = (uint8_t)name_len;
                new_entry->file_type = file_type;
                memcpy((uint8_t *)new_entry + header_size, name, name_len);
                return write_block(volume, data_block, g_block_buffer);
            }

            cursor += entry->rec_len;
        }
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        if (parent_inode->i_block[block_index] == 0)
        {
            uint32_t new_block;
            ext2_dir_entry_t *entry;

            if (!alloc_block(volume, &new_block))
            {
                return false;
            }

            memset(g_block_buffer, 0, volume->block_size);
            entry = (ext2_dir_entry_t *)g_block_buffer;
            entry->inode = child_inode_number;
            entry->rec_len = (uint16_t)volume->block_size;
            entry->name_len = (uint8_t)name_len;
            entry->file_type = file_type;
            memcpy((uint8_t *)entry + header_size, name, name_len);

            parent_inode->i_block[block_index] = new_block;
            parent_inode->i_size += volume->block_size;
            parent_inode->i_blocks += volume->sectors_per_block;
            return write_block(volume, new_block, g_block_buffer) && write_inode(volume, parent_inode_number, parent_inode);
        }
    }

    return false;
}

/**
 * updates the name of an existing directory entry within a specified directory inode, ensuring that the new name fits within the allocated space for the entry.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory containing the entry to be renamed.
 * @param old_name the current name of the directory entry to be renamed.
 * @param old_name_len the length of the current name of the directory entry.
 * @param new_name the new name to assign to the directory entry.
 * @param new_name_len the length of the new name to assign to the directory entry.
 * @return true if the directory entry name was successfully updated, false if the entry was not found, the new name does not fit, or an error occurred during the operation.
 */
bool update_directory_entry_name(ext2_volume_t *volume, uint32_t dir_inode_number, const char *old_name, uint32_t old_name_len, const char *new_name, uint32_t new_name_len)
{
    ext2_inode_t dir_inode;
    uint32_t block_index;
    uint32_t entry_offset;
    ext2_dir_entry_t entry;
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;
    uint32_t needed = ext2_dir_entry_size(new_name_len);

    if (!volume || !old_name || !new_name || new_name_len == 0 || new_name_len >= 255)
    {
        return false;
    }

    if (kerr_failed(ext2_read_inode(volume, dir_inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (!find_directory_entry(volume, dir_inode_number, old_name, old_name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[block_index], g_block_buffer))
    {
        return false;
    }

    if (needed > entry.rec_len)
    {
        return false;
    }

    ((ext2_dir_entry_t *)(g_block_buffer + entry_offset))->name_len = (uint8_t)new_name_len;
    ((ext2_dir_entry_t *)(g_block_buffer + entry_offset))->file_type = entry.file_type;
    memcpy(g_block_buffer + entry_offset + header_size, new_name, new_name_len);
    return write_block(volume, dir_inode.i_block[block_index], g_block_buffer);
}

/**
 * checks if a directory represented by a given inode number is empty, meaning it contains no entries other than the standard '.' and '..' entries.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number of the directory to check.
 * @return true if the directory is empty (only contains '.' and '..'), false if it contains other entries or if an error occurred during the check.
 */
bool directory_is_empty(ext2_volume_t *volume, uint32_t inode_number)
{
    ext2_inode_t dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;

    if (kerr_failed(ext2_read_inode(volume, inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    for (block_index = 0; block_index < EXT2_NDIR_BLOCKS; block_index++)
    {
        uint32_t data_block = dir_inode.i_block[block_index];
        uint32_t offset = 0;

        if (data_block == 0)
        {
            continue;
        }

        if (!read_block(volume, data_block, g_block_buffer))
        {
            return false;
        }

        while (offset + header_size <= volume->block_size)
        {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return false;
            }

            if (entry->inode != 0)
            {
                const char *entry_name = (const char *)(g_block_buffer + offset + header_size);

                if (!(entry->name_len == 1u && entry_name[0] == '.') && !(entry->name_len == 2u && entry_name[0] == '.' && entry_name[1] == '.'))
                {
                    return false;
                }
            }

            offset += entry->rec_len;
        }
    }

    return true;
}

/**
 * updates the '..' entry in a directory to point to a new parent inode number, effectively changing the parent directory reference for that directory.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory whose parent link is to be updated.
 * @param parent_inode_number the inode number of the new parent directory to which the '..' entry should point.
 * @return true if the parent link was successfully updated, false if the directory inode could not be read, the directory is invalid, or an error occurred during the update process.
 */
bool update_directory_parent_link(ext2_volume_t *volume, uint32_t dir_inode_number, uint32_t parent_inode_number)
{
    ext2_inode_t dir_inode;
    ext2_dir_entry_t *entry;
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;

    if (kerr_failed(ext2_read_inode(volume, dir_inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (dir_inode.i_block[0] == 0)
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[0], g_block_buffer))
    {
        return false;
    }

    entry = (ext2_dir_entry_t *)(g_block_buffer + ext2_dir_entry_size(1u));
    if (entry->name_len != 2u)
    {
        return false;
    }

    if (memcmp((uint8_t *)entry + header_size, "..", 2u) != 0)
    {
        return false;
    }

    entry->inode = parent_inode_number;
    return write_block(volume, dir_inode.i_block[0], g_block_buffer);
}

/**
 * removes a directory entry with a specific name from a given directory inode, effectively deleting the entry from the directory's data blocks.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory from which the entry will be removed.
 * @param name the name of the directory entry to be removed.
 * @param name_len the length of the name of the directory entry to be removed.
 * @return true if the directory entry was successfully removed, false if the entry was not found, the directory inode could not be read, or an error occurred during the removal process.
 */
bool remove_directory_entry(ext2_volume_t *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len)
{
    ext2_inode_t dir_inode;
    uint32_t block_index;
    uint32_t entry_offset;
    ext2_dir_entry_t entry;

    if (kerr_failed(ext2_read_inode(volume, dir_inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return false;
    }

    if (!find_directory_entry(volume, dir_inode_number, name, name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (!read_block(volume, dir_inode.i_block[block_index], g_block_buffer))
    {
        return false;
    }

    entry = *(ext2_dir_entry_t *)(g_block_buffer + entry_offset);
    entry.inode = 0;
    entry.name_len = 0;
    entry.file_type = 0;
    memcpy(g_block_buffer + entry_offset, &entry, (uint32_t)sizeof(ext2_dir_entry_t));
    return write_block(volume, dir_inode.i_block[block_index], g_block_buffer);
}

/**
 * initializes a new inode structure for a regular file, setting its mode, link count, and clearing other fields to prepare it for use in the filesystem.
 * @param inode pointer to the ext2_inode_t structure to be initialized.
 * @return true if the inode was successfully initialized, false if the inode pointer was null.
 */
bool setup_new_file_inode(ext2_inode_t *inode)
{
    if (!inode)
    {
        return false;
    }

    memset(inode, 0, (uint32_t)sizeof(ext2_inode_t));
    inode->i_mode = EXT2_S_IFREG | 0644u;
    inode->i_links_count = 1u;
    return true;
}

/**
 * initializes a new inode structure for a directory, setting its mode, link count, size, and block count to prepare it for use in the filesystem.
 * @param inode pointer to the ext2_inode_t structure to be initialized.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume, used to determine the block size and sectors per block for the directory inode.
 * @return true if the inode was successfully initialized, false if either the inode or volume pointers were null.
 */
bool setup_new_dir_inode(ext2_inode_t *inode, ext2_volume_t *volume)
{
    if (!inode || !volume)
    {
        return false;
    }

    memset(inode, 0, (uint32_t)sizeof(ext2_inode_t));
    inode->i_mode = EXT2_S_IFDIR | 0755u;
    inode->i_links_count = 2u;
    inode->i_size = volume->block_size;
    inode->i_blocks = volume->sectors_per_block;
    return true;
}

/**
 * initializes a new directory block with the standard '.' and '..' entries, setting their inode numbers and ensuring proper formatting for the directory structure.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number of the directory being initialized (for the '.' entry).
 * @param parent_inode_number the inode number of the parent directory (for the '..' entry).
 * @param block_number the block number where the new directory block will be written.
 * @return true if the directory block was successfully initialized and written to the specified block number, false if an error occurred during the initialization or writing process, or if the volume pointer was null or the block size was insufficient to hold the directory entries.
 */
bool initialize_directory_block(ext2_volume_t *volume, uint32_t inode_number, uint32_t parent_inode_number, uint32_t block_number)
{
    ext2_dir_entry_t *dot;
    ext2_dir_entry_t *dotdot;
    uint32_t header_size = OFFSETOF(ext2_dir_entry_t, file_type) + 1;
    uint32_t dot_size = ext2_dir_entry_size(1u);

    if (!volume || volume->block_size < (dot_size + header_size + 1u))
    {
        return false;
    }

    memset(g_block_buffer, 0, volume->block_size);
    dot = (ext2_dir_entry_t *)g_block_buffer;
    dot->inode = inode_number;
    dot->rec_len = (uint16_t)dot_size;
    dot->name_len = 1u;
    dot->file_type = EXT2_FT_DIR;
    memcpy((uint8_t *)dot + header_size, ".", 1u);

    dotdot = (ext2_dir_entry_t *)(g_block_buffer + dot_size);
    dotdot->inode = parent_inode_number;
    dotdot->rec_len = (uint16_t)(volume->block_size - dot_size);
    dotdot->name_len = 2u;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy((uint8_t *)dotdot + header_size, "..", 2u);

    return write_block(volume, block_number, g_block_buffer);
}

/**
 * looks up the parent directory inode and the name of the final component in a given path, splitting the path into its parent and child components and verifying that the parent is a valid directory.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param path the input path string to be looked up (must start with '/').
 * @param parent_inode_out pointer to a variable where the inode number of the parent directory will be stored.
 * @param name_out buffer to store the name of the final component (file or directory) in the path.
 * @param name_out_size the size of the name_out buffer in bytes.
 * @param parent_path buffer to store the parent directory path.
 * @param parent_path_size the size of the parent_path buffer in bytes.
 * @return true if the parent inode and name were successfully looked up and stored in the output parameters, false if the path was invalid, the parent directory could not be found, or the parent inode is not a valid directory.
 */
bool lookup_parent_and_name(ext2_volume_t *volume, const char *path, uint32_t *parent_inode_out, char *name_out, uint32_t name_out_size, char *parent_path, uint32_t parent_path_size)
{
    uint32_t parent_inode;
    ext2_inode_t parent_inode_cache;

    if (!volume || !path || !parent_inode_out || !name_out || !parent_path)
    {
        return false;
    }

    if (!split_path(path, parent_path, parent_path_size, name_out, name_out_size))
    {
        return false;
    }

    if (ext2_lookup_path(volume, parent_path, &parent_inode) != KERR_OK)
    {
        return false;
    }

    if (kerr_failed(ext2_read_inode(volume, parent_inode, &parent_inode_cache)) || !inode_is_dir(&parent_inode_cache))
    {
        return false;
    }

    *parent_inode_out = parent_inode;
    return true;
}

/**
 * looks up a child directory entry by name within a specified parent directory inode, returning the child's inode number and file type if found.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param parent_inode_number the inode number of the parent directory to search within.
 * @param name the name of the child directory entry to look up.
 * @param name_len the length of the name of the child directory entry.
 * @param child_inode_out pointer to a variable where the inode number of the found child entry will be stored.
 * @param child_type_out pointer to a variable where the file type of the found child entry will be stored.
 * @return true if the child directory entry was found and its details are stored in the output parameters, false if the entry was not found or an error occurred during the lookup process.
 */
bool lookup_child_type(ext2_volume_t *volume, uint32_t parent_inode_number, const char *name, uint32_t name_len, uint32_t *child_inode_out, uint8_t *child_type_out)
{
    uint32_t block_index;
    uint32_t entry_offset;
    ext2_dir_entry_t entry;

    if (!find_directory_entry(volume, parent_inode_number, name, name_len, &block_index, &entry_offset, &entry))
    {
        return false;
    }

    if (child_inode_out)
    {
        *child_inode_out = entry.inode;
    }

    if (child_type_out)
    {
        *child_type_out = entry.file_type;
    }

    return true;
}

/**
 * frees the blocks associated with a given inode and then frees the inode itself, effectively removing the file or directory represented by that inode from the filesystem.
 * @param volume pointer to the ext2_volume_t structure representing the filesystem volume.
 * @param inode_number the inode number of the file or directory to be freed.
 * @param inode pointer to the ext2_inode_t structure representing the inode to be freed. If null, the function will read the inode from the filesystem.
 * @return true if the blocks and inode were successfully freed, false if an error occurred during the process or if the volume pointer was null.
 */
bool free_inode_and_blocks(ext2_volume_t *volume, uint32_t inode_number, ext2_inode_t *inode)
{
    ext2_inode_t cache;

    if (!volume)
    {
        return false;
    }

    if (inode)
    {
        cache = *inode;
    }
    else if (kerr_failed(ext2_read_inode(volume, inode_number, &cache)))
    {
        return false;
    }

    if (!free_inode_block_chain(volume, &cache))
    {
        return false;
    }

    return free_inode(volume, inode_number);
}
