#include "ext2_internal.h"

/**
 * determines if a given inode represents a directory by checking its mode against the directory file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a directory, false otherwise.
 */
bool inode_is_dir(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * determines if a given inode represents a regular file by checking its mode against the regular file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a regular file, false otherwise.
 */
bool inode_is_regular(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

/**
 * determines if a given inode represents a directory by checking its mode against the directory file type constant.
 * @param inode pointer to the EXT2_INODE structure to be checked.
 * @return true if the inode represents a directory, false otherwise.
 */
bool inode_is_directory(const EXT2_INODE *inode)
{
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/**
 * maps an inode's mode to the corresponding EXT2 file type constant, returning the appropriate value for directory or regular file types, or 0 for unsupported types.
 * @param inode pointer to the EXT2_INODE structure whose file type is to be determined.
 * @return the EXT2 file type constant corresponding to the inode's mode, or 0 if the inode does not represent a directory or regular file.
 */
uint8_t inode_to_file_type(const EXT2_INODE *inode)
{
    if (inode_is_directory(inode))
    {
        return EXT2_FT_DIR;
    }

    if (inode_is_regular(inode))
    {
        return EXT2_FT_REG_FILE;
    }

    return 0;
}

/**
 * searches for a directory entry with a specific name within a given directory inode, returning the inode number of the found entry if it exists.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param dir_inode_number the inode number of the directory to search within.
 * @param name the name of the directory entry to search for.
 * @param name_len the length of the name to search for.
 * @param inode_out pointer to a variable where the inode number of the found entry will be stored if the entry is found.
 * @return KERR_OK if the entry was found and its inode number is stored in inode_out, KERR_NOTFOUND if the entry does not exist, or KERR_IO if an error occurred during the search or if the directory inode could not be read.
 */
kerr_t find_in_directory(EXT2_VOLUME *volume, uint32_t dir_inode_number, const char *name, uint32_t name_len, uint32_t *inode_out)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!inode_out)
    {
        return KERR_IO;
    }

    if (kerr_failed(EXT2_ReadInode(volume, dir_inode_number, &dir_inode)) || !inode_is_dir(&dir_inode))
    {
        return KERR_IO;
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
            return KERR_IO;
        }

        while (offset + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;
            const char *entry_name;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return KERR_IO;
            }

            entry_name = (const char *)(g_block_buffer + offset + header_size);
            if (entry->inode != 0 && name_len == entry->name_len && memcmp(name, entry_name, name_len) == 0)
            {
                *inode_out = entry->inode;
                return KERR_OK;
            }

            offset += entry->rec_len;
        }
    }

    return KERR_NOTFOUND;
}

/**
 * resolves a logical block index within an inode to its corresponding physical block number on disk, handling direct, single indirect, double indirect, and triple indirect block addressing as defined by the EXT2 filesystem structure.
 * @param volume pointer to the EXT2_VOLUME structure representing the filesystem volume.
 * @param inode pointer to the EXT2_INODE structure representing the inode whose block is being resolved.
 * @param logical_block_index the logical block index to resolve (0-based).
 * @param physical_block_out pointer to a variable where the resolved physical block number will be stored.
 * @return true if the logical block index was successfully resolved to a physical block number, false if an error occurred during the resolution process or if the logical block index is out of range for the given inode.
 */
bool resolve_data_block(EXT2_VOLUME *volume, const EXT2_INODE *inode, uint32_t logical_block_index, uint32_t *physical_block_out)
{
    uint32_t ptrs_per_block = volume->block_size / 4u;

    // direct blocks: i_block[0..11]
    if (logical_block_index < EXT2_NDIR_BLOCKS)
    {
        *physical_block_out = inode->i_block[logical_block_index];
        return true;
    }

    logical_block_index -= EXT2_NDIR_BLOCKS;

    // single indirect: i_block[12] -> block of pointers -> data block
    if (logical_block_index < ptrs_per_block)
    {
        uint32_t indirect = inode->i_block[12];

        if (indirect == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, indirect, g_block_buffer2))
            return false;

        *physical_block_out = ((uint32_t *)g_block_buffer2)[logical_block_index];
        return true;
    }

    logical_block_index -= ptrs_per_block;

    // double indirect: i_block[13] -> L1 block of pointers -> L2 block of pointers -> data block
    if (logical_block_index < ptrs_per_block * ptrs_per_block)
    {
        uint32_t l1_index = logical_block_index / ptrs_per_block;
        uint32_t l2_index = logical_block_index % ptrs_per_block;
        uint32_t l1_block;
        uint32_t l2_block;

        if (inode->i_block[13] == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, inode->i_block[13], g_block_buffer3))
            return false;

        l1_block = ((uint32_t *)g_block_buffer3)[l1_index];
        if (l1_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l1_block, g_block_buffer2))
            return false;

        l2_block = ((uint32_t *)g_block_buffer2)[l2_index];
        *physical_block_out = l2_block;
        return true;
    }

    logical_block_index -= ptrs_per_block * ptrs_per_block;

    // triple indirect: i_block[14] -> L1 block -> L2 block -> L3 block -> data block
    {
        uint32_t l1_index = logical_block_index / (ptrs_per_block * ptrs_per_block);
        uint32_t remainder = logical_block_index % (ptrs_per_block * ptrs_per_block);
        uint32_t l2_index = remainder / ptrs_per_block;
        uint32_t l3_index = remainder % ptrs_per_block;
        uint32_t l1_block;
        uint32_t l2_block;
        uint32_t l3_block;

        if (inode->i_block[14] == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, inode->i_block[14], g_block_buffer4))
            return false;

        l1_block = ((uint32_t *)g_block_buffer4)[l1_index];
        if (l1_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l1_block, g_block_buffer3))
            return false;

        l2_block = ((uint32_t *)g_block_buffer3)[l2_index];
        if (l2_block == 0)
        {
            *physical_block_out = 0;
            return true;
        }

        if (!read_block(volume, l2_block, g_block_buffer2))
            return false;

        l3_block = ((uint32_t *)g_block_buffer2)[l3_index];
        *physical_block_out = l3_block;
        return true;
    }
}

kerr_t EXT2_ReadInode(EXT2_VOLUME *volume, uint32_t inode_number, EXT2_INODE *inode_out)
{
    EXT2_BLOCK_GROUP_DESC bgd;
    uint32_t zero_based;
    uint32_t group;
    uint32_t index;
    uint32_t inode_byte_offset;

    if (!volume || !inode_out || inode_number == 0)
    {
        return KERR_INVAL;
    }

    zero_based = inode_number - 1u;
    group = zero_based / volume->inodes_per_group;
    index = zero_based % volume->inodes_per_group;

    if (group >= volume->block_group_count)
    {
        return KERR_INVAL;
    }

    if (!read_group_desc(volume, group, &bgd))
    {
        return KERR_IO;
    }

    inode_byte_offset = (bgd.bg_inode_table * volume->block_size) + (index * volume->inode_size);
    if (!read_abs_bytes(volume->disk, inode_byte_offset, volume->inode_size, g_inode_buffer))
    {
        return KERR_IO;
    }

    memcpy(inode_out, g_inode_buffer, (uint32_t)sizeof(EXT2_INODE));
    return KERR_OK;
}

kerr_t EXT2_ListDirectory(EXT2_VOLUME *volume, uint32_t inode_number)
{
    EXT2_INODE dir_inode;
    uint32_t block_index;
    uint32_t header_size = OFFSETOF(EXT2_DIR_ENTRY, file_type) + 1;

    if (!volume)
    {
        return KERR_INVAL;
    }

    if (kerr_failed(EXT2_ReadInode(volume, inode_number, &dir_inode)) || !inode_is_directory(&dir_inode))
    {
        return KERR_IO;
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
            return KERR_IO;
        }

        while (offset + header_size <= volume->block_size)
        {
            EXT2_DIR_ENTRY *entry = (EXT2_DIR_ENTRY *)(g_block_buffer + offset);
            uint32_t min_size = header_size + entry->name_len;

            if (entry->rec_len < min_size || entry->rec_len == 0 || offset + entry->rec_len > volume->block_size)
            {
                return KERR_IO;
            }

            if (entry->inode != 0 && entry->name_len > 0 && entry->name_len < 240)
            {
                char name[240];
                const char *src = (const char *)(g_block_buffer + offset + header_size);

                memcpy(name, src, entry->name_len);
                name[entry->name_len] = 0;
                printf("  %s\r\n", name);
            }

            offset += entry->rec_len;
        }
    }

    return KERR_OK;
}

kerr_t EXT2_LookupPath(EXT2_VOLUME *volume, const char *path, uint32_t *inode_out)
{
    uint32_t current = EXT2_INODE_ROOT;
    uint32_t i = 0;

    if (!volume || !path || !inode_out)
    {
        return KERR_IO;
    }

    if (path[0] == 0)
    {
        return KERR_NOTFOUND;
    }

    while (path[i] == '/')
    {
        i++;
    }

    if (path[i] == 0)
    {
        *inode_out = EXT2_INODE_ROOT;
        return KERR_OK;
    }

    while (path[i] != 0)
    {
        uint32_t start = i;
        uint32_t len;
        uint32_t next_inode;

        while (path[i] != 0 && path[i] != '/')
        {
            i++;
        }

        len = i - start;
        if (len == 0)
        {
            while (path[i] == '/')
            {
                i++;
            }
            continue;
        }

        {
            kerr_t status = find_in_directory(volume, current, &path[start], len, &next_inode);
            if (status != KERR_OK)
            {
                return status;
            }
        }

        current = next_inode;

        while (path[i] == '/')
        {
            i++;
        }
    }

    *inode_out = current;
    return KERR_OK;
}
