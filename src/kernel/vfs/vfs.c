#include "vfs.h"

#include "../../common/string.h"

static FS_MOUNT *root_mount = 0;

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

bool vfs_resolve_path(
    const char *cwd,
    const char *input,
    char *output,
    uint32_t size)
{
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

    if (input[0] == '/')
    {
        input_len = strlen(input);

        if (input_len + 1 > size)
        {
            return false;
        }

        strcpy(output, input);
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

    if (needed > size)
    {
        return false;
    }

    pos = 0;

    output[pos++] = '/';

    for (i = cwd_start; i < cwd_len; i++)
    {
        output[pos++] = cwd[i];
    }

    if (output[pos - 1] != '/')
    {
        output[pos++] = '/';
    }

    for (i = 0; i < input_len; i++)
    {
        output[pos++] = input[i];
    }

    output[pos] = '\0';

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

bool vfs_is_directory(const char *path)
{
    VFS_FILE file;

    if (!vfs_open(path, &file))
    {
        return false;
    }

    bool result =
        vfs_file_type(&file) == FS_TYPE_DIR;

    vfs_close(&file);

    return result;
}