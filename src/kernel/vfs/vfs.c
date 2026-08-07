#include "vfs.h"

#include "../../common/string.h"

static FS_MOUNT *root_mount = 0; // pointer to the root filesystem mount structure, initialized during kernel startup

/**
 * normalize an absolute path by resolving '.' and '..' components.
 * @param input absolute path to normalize.
 * @param output destination buffer for normalized path.
 * @param size size of destination buffer in bytes.
 * @return true on success, false on invalid input or insufficient output size.
 */
static bool vfs_normalize_absolute_path(
    const char *input,
    char *output,
    uint32_t size)
{
    uint32_t i;
    uint32_t out_pos;
    uint32_t depth;
    uint32_t component_start[128];

    if (!input || !output || size < 2)
    {
        return false;
    }

    if (input[0] != '/')
    {
        return false;
    }

    output[0] = '/';
    output[1] = '\0';
    out_pos = 1;
    depth = 0;

    i = 0;
    while (input[i] != '\0')
    {
        uint32_t start;
        uint32_t len;
        uint32_t j;

        while (input[i] == '/')
        {
            i++;
        }

        if (input[i] == '\0')
        {
            break;
        }

        start = i;
        while (input[i] != '\0' && input[i] != '/')
        {
            i++;
        }

        len = i - start;

        if (len == 1 && input[start] == '.')
        {
            continue;
        }

        if (len == 2 && input[start] == '.' && input[start + 1] == '.')
        {
            if (depth > 0)
            {
                out_pos = component_start[depth - 1];
                if (out_pos > 1)
                {
                    out_pos--;
                }

                output[out_pos] = '\0';
                depth--;
            }

            continue;
        }

        if (depth >= (uint32_t)(sizeof(component_start) / sizeof(component_start[0])))
        {
            return false;
        }

        if (out_pos > 1)
        {
            if (out_pos + 1 >= size)
            {
                return false;
            }

            output[out_pos++] = '/';
        }

        component_start[depth++] = out_pos;

        if (out_pos + len + 1 > size)
        {
            return false;
        }

        for (j = 0; j < len; j++)
        {
            output[out_pos++] = input[start + j];
        }

        output[out_pos] = '\0';
    }

    return true;
}

bool vfs_mount_root(FS_MOUNT *mount)
{
    if (!mount || !mount->is_mounted)
    {
        return false;
    }

    root_mount = mount;

    return true;
}

bool vfs_open(
    const char *path,
    VFS_FILE *file)
{
    if (!root_mount || !file || !path)
        return false;

    if (!fs_open(
            root_mount,
            path,
            &file->file))
    {
        file->mount = 0;
        return false;
    }

    file->mount = root_mount;
    return true;
}

bool vfs_create(const char *path)
{
    if (!root_mount || !path)
    {
        return false;
    }

    return fs_create(root_mount, path);
}

bool vfs_mkdir(const char *path)
{
    if (!root_mount || !path)
    {
        return false;
    }

    return fs_mkdir(root_mount, path);
}

bool vfs_remove(const char *path)
{
    if (!root_mount || !path)
    {
        return false;
    }

    return fs_remove(root_mount, path);
}

bool vfs_rmdir(const char *path)
{
    if (!root_mount || !path)
    {
        return false;
    }

    return fs_rmdir(root_mount, path);
}

bool vfs_rename(const char *old_path, const char *new_path)
{
    if (!root_mount || !old_path || !new_path)
    {
        return false;
    }

    return fs_rename(root_mount, old_path, new_path);
}

VFS_STATUS vfs_stat(const char *path, VFS_STAT *stat_out)
{
    if (!root_mount || !path || !stat_out)
    {
        return VFS_IO_ERROR;
    }

    return (VFS_STATUS)fs_stat(root_mount, path, stat_out);
}

bool vfs_resolve_path(
    const char *cwd,
    const char *input,
    char *output,
    uint32_t size)
{
    char combined[512];
    char normalized[512];
    uint32_t cwd_len;
    uint32_t cwd_start;
    uint32_t input_len;
    uint32_t i;
    uint32_t pos;
    uint32_t needed;

    if (!cwd || !input || !output || size == 0)
    {
        return false;
    }

    if (input[0] == '\0')
    {
        return false;
    }

    if (size > sizeof(normalized))
    {
        return false;
    }

    if (input[0] == '/')
    {
        if (!vfs_normalize_absolute_path(input, normalized, sizeof(normalized)))
        {
            return false;
        }

        if (strlen(normalized) + 1 > size)
        {
            return false;
        }

        strcpy(output, normalized);
        return true;
    }

    cwd_len = strlen(cwd);
    input_len = strlen(input);

    if (cwd_len == 0)
    {
        return false;
    }

    cwd_start = 0;
    while (cwd_start < cwd_len && cwd[cwd_start] == '/')
    {
        cwd_start++;
    }

    if (cwd_start == cwd_len)
    {
        cwd_start = cwd_len - 1;
    }

    needed = 1 + (cwd_len - cwd_start) + input_len + 1;
    if (cwd_len > cwd_start && cwd[cwd_len - 1] != '/')
    {
        needed++;
    }

    if (needed > sizeof(combined))
    {
        return false;
    }

    pos = 0;
    combined[pos++] = '/';

    for (i = cwd_start; i < cwd_len; i++)
    {
        combined[pos++] = cwd[i];
    }

    if (combined[pos - 1] != '/')
    {
        combined[pos++] = '/';
    }

    for (i = 0; i < input_len; i++)
    {
        combined[pos++] = input[i];
    }

    combined[pos] = '\0';

    if (!vfs_normalize_absolute_path(combined, normalized, sizeof(normalized)))
    {
        return false;
    }

    if (strlen(normalized) + 1 > size)
    {
        return false;
    }

    strcpy(output, normalized);
    return true;
}

bool vfs_join_path(
    const char *base,
    const char *name,
    char *out,
    uint32_t size)
{
    uint32_t base_len;
    uint32_t name_len;
    uint32_t i;
    uint32_t pos;
    uint32_t needed;

    if (!base || !name || !out || size == 0)
    {
        return false;
    }

    if (base[0] == '\0' || name[0] == '\0')
    {
        return false;
    }

    base_len = strlen(base);
    name_len = strlen(name);

    needed = base_len + name_len + 1;
    if (base[base_len - 1] != '/')
    {
        needed++;
    }

    if (needed > size)
    {
        return false;
    }

    pos = 0;
    for (i = 0; i < base_len; i++)
    {
        out[pos++] = base[i];
    }

    if (out[pos - 1] != '/')
    {
        out[pos++] = '/';
    }

    for (i = 0; i < name_len; i++)
    {
        out[pos++] = name[i];
    }

    out[pos] = '\0';
    return true;
}

uint32_t vfs_read(
    VFS_FILE *file,
    uint32_t size,
    void *buffer)
{
    if (!file || !file->mount)
    {
        return 0;
    }

    return fs_read(
        &file->file,
        size,
        buffer);
}

bool vfs_read_entry(
    VFS_FILE *file,
    FS_DIRENT *entryOut)
{
    if (!file || !file->mount || !entryOut)
    {
        return false;
    }

    return fs_read_entry(&file->file, entryOut);
}

void vfs_close(
    VFS_FILE *file)
{
    if (!file)
    {
        return;
    }

    fs_close(&file->file);
    file->mount = 0;
}

uint8_t vfs_file_type(
    VFS_FILE *file)
{
    if (!file || !file->mount)
    {
        return FS_TYPE_UNKNOWN;
    }

    return fs_file_type(&file->file);
}

uint8_t vfs_type(
    VFS_FILE *file)
{
    return vfs_file_type(file);
}