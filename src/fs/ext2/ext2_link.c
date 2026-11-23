#include "ext2_internal.h"

#include "common/memory.h"
#include "common/string.h"

/**
 * @file ext2_link.c
 * @brief hard links and symbolic links.
 *
 * a hard link is one more directory entry for an inode: the inode counts
 * its names in i_links_count, and its data stays until the last name goes.
 *
 * a symbolic link is an inode of type S_IFLNK whose data is the path it
 * points at. a short path (shorter than the 60 bytes of i_block) is kept
 * in i_block itself, with no data block (a "fast" link, i_blocks 0). a
 * longer path is in one data block, so a target is shorter than a block.
 */

// the bytes of i_block, where a fast link keeps its target
#define FAST_LINK_MAX ((uint32_t)sizeof(((ext2_inode_t *)0)->i_block))

bool inode_is_symlink(const ext2_inode_t *inode)
{
    return inode && (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK;
}

bool inode_is_fast_symlink(const ext2_inode_t *inode)
{
    return inode_is_symlink(inode) && inode->i_blocks == 0;
}

/**
 * read the target of a symbolic link inode.
 * @param target gets the path and a NUL.
 * @return KERR_OK, KERR_RANGE (the target does not fit in size), or KERR_IO.
 */
static kerr_t read_target(ext2_volume_t *volume, const ext2_inode_t *inode, char *target,
                          uint32_t size)
{
    uint32_t length = inode->i_size;
    if (length >= size)
    {
        return KERR_RANGE;
    }

    if (inode_is_fast_symlink(inode))
    {
        if (length >= FAST_LINK_MAX)
        {
            return KERR_IO;
        }
        memcpy(target, inode->i_block, length);
    }
    else
    {
        uint32_t block;
        if (length >= volume->block_size || !resolve_data_block(volume, inode, 0, &block) ||
            block == 0 || !read_block(volume, block, volume->buf_block))
        {
            return KERR_IO;
        }
        memcpy(target, volume->buf_block, length);
    }
    target[length] = '\0';
    return KERR_OK;
}

kerr_t ext2_find_link(ext2_volume_t *volume, const char *path, bool follow_last, char *target,
                      uint32_t size, uint32_t *end_out)
{
    uint32_t current = EXT2_INODE_ROOT;
    uint32_t i = 0;

    if (!volume || !path || !target || !end_out)
    {
        return KERR_INVAL;
    }
    *end_out = 0;

    while (path[i] != '\0')
    {
        while (path[i] == '/')
        {
            i++;
        }
        uint32_t start = i;
        while (path[i] != '\0' && path[i] != '/')
        {
            i++;
        }
        if (i == start)
        {
            break; // trailing slashes
        }

        uint32_t next;
        if (find_in_directory(volume, current, &path[start], i - start, &next) != KERR_OK)
        {
            return KERR_OK; // a missing name: the operation itself reports it
        }

        uint32_t after = i;
        while (path[after] == '/')
        {
            after++;
        }
        bool last = path[after] == '\0';

        ext2_inode_t inode;
        if (kerr_failed(ext2_read_inode(volume, next, &inode)))
        {
            return KERR_IO;
        }
        if (inode_is_symlink(&inode) && (follow_last || !last))
        {
            kerr_t err = read_target(volume, &inode, target, size);
            if (kerr_failed(err))
            {
                return err;
            }
            *end_out = i;
            return KERR_OK;
        }
        if (!inode_is_dir(&inode))
        {
            return KERR_OK; // a file in the middle: the operation reports it
        }
        current = next;
    }
    return KERR_OK;
}

kerr_t ext2_readlink(ext2_volume_t *volume, const char *path, char *target, uint32_t size)
{
    uint32_t inode_number;
    ext2_inode_t inode;

    if (!volume || !path || !target)
    {
        return KERR_INVAL;
    }
    kerr_t err = ext2_lookup_path(volume, path, &inode_number);
    if (kerr_failed(err))
    {
        return err;
    }
    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)))
    {
        return KERR_IO;
    }
    if (!inode_is_symlink(&inode))
    {
        return KERR_INVAL;
    }
    return read_target(volume, &inode, target, size);
}

/**
 * add the entry name in the parent for a new or linked inode, and mark the
 * parent changed.
 */
static kerr_t add_entry(ext2_volume_t *volume, uint32_t parent_number, const char *name,
                        uint32_t inode_number, uint8_t file_type)
{
    ext2_inode_t parent;
    if (kerr_failed(ext2_read_inode(volume, parent_number, &parent)) ||
        !inode_is_directory(&parent))
    {
        return KERR_IO;
    }
    if (!append_or_replace_directory_entry(volume, parent_number, &parent, name,
                                           (uint32_t)strlen(name), inode_number, file_type))
    {
        return KERR_NOSPC;
    }
    parent.i_mtime = ext2_now();
    parent.i_ctime = parent.i_mtime;
    return write_inode(volume, parent_number, &parent) ? KERR_OK : KERR_IO;
}

/**
 * find the parent of a new name; the name must not exist yet.
 */
static kerr_t new_name(ext2_volume_t *volume, const char *path, uint32_t *parent_out,
                       char *name, uint32_t name_size)
{
    char parent_path[EXT2_NAME_MAX];
    if (!lookup_parent_and_name(volume, path, parent_out, name, name_size, parent_path,
                                sizeof(parent_path)))
    {
        return KERR_NOTFOUND;
    }
    if (lookup_child_type(volume, *parent_out, name, (uint32_t)strlen(name), 0, 0))
    {
        return KERR_EXIST;
    }
    return KERR_OK;
}

kerr_t ext2_symlink(ext2_volume_t *volume, const char *path, const char *target)
{
    char name[EXT2_NAME_MAX];
    uint32_t parent_number;
    uint32_t inode_number;
    uint32_t block = 0;
    ext2_inode_t inode;

    if (!volume || !path || !target)
    {
        return KERR_INVAL;
    }
    uint32_t length = (uint32_t)strlen(target);
    if (length == 0)
    {
        return KERR_NOTFOUND;
    }
    if (length >= volume->block_size)
    {
        return KERR_RANGE;
    }

    kerr_t err = new_name(volume, path, &parent_number, name, sizeof(name));
    if (kerr_failed(err))
    {
        return err;
    }
    if (!alloc_inode(volume, &inode_number))
    {
        return KERR_NOSPC;
    }

    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFLNK | 0777u;
    inode.i_links_count = 1;
    inode.i_atime = inode.i_ctime = inode.i_mtime = ext2_now();
    inode.i_size = length;
    if (length < FAST_LINK_MAX)
    {
        memcpy(inode.i_block, target, length);
    }
    else
    {
        if (!alloc_block(volume, &block))
        {
            free_inode(volume, inode_number);
            return KERR_NOSPC;
        }
        memset(volume->buf_block, 0, volume->block_size);
        memcpy(volume->buf_block, target, length);
        if (!write_block(volume, block, volume->buf_block))
        {
            free_block(volume, block);
            free_inode(volume, inode_number);
            return KERR_IO;
        }
        inode.i_block[0] = block;
        inode.i_blocks = volume->sectors_per_block;
    }

    if (!write_inode(volume, inode_number, &inode))
    {
        err = KERR_IO;
    }
    else
    {
        err = add_entry(volume, parent_number, name, inode_number, EXT2_FT_SYMLINK);
    }
    if (kerr_failed(err))
    {
        if (block)
        {
            free_block(volume, block);
        }
        free_inode(volume, inode_number);
    }
    return err;
}

kerr_t ext2_link(ext2_volume_t *volume, const char *old_path, const char *new_path)
{
    char name[EXT2_NAME_MAX];
    uint32_t parent_number;
    uint32_t inode_number;
    ext2_inode_t inode;

    if (!volume || !old_path || !new_path)
    {
        return KERR_INVAL;
    }
    kerr_t err = ext2_lookup_path(volume, old_path, &inode_number);
    if (kerr_failed(err))
    {
        return err;
    }
    if (kerr_failed(ext2_read_inode(volume, inode_number, &inode)))
    {
        return KERR_IO;
    }
    if (inode_is_dir(&inode))
    {
        return KERR_ISDIR; // a directory has one name only
    }
    if (inode.i_links_count >= EXT2_LINK_MAX)
    {
        return KERR_RANGE;
    }

    err = new_name(volume, new_path, &parent_number, name, sizeof(name));
    if (kerr_failed(err))
    {
        return err;
    }
    err = add_entry(volume, parent_number, name, inode_number, inode_to_file_type(&inode));
    if (kerr_failed(err))
    {
        return err;
    }

    inode.i_links_count++;
    inode.i_ctime = ext2_now();
    return write_inode(volume, inode_number, &inode) ? KERR_OK : KERR_IO;
}
