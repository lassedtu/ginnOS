#include "kernel.h"
#include "../common/stdio.h"
#include "../common/string.h"
#include "../common/memory.h"
#include "../drivers/disk/ata.h"
#include "fs/fs.h"

#define MAX_PATH_LENGTH 512u
#define MAX_TREE_DEPTH 16u

static bool is_dot_name(const char *name)
{
    return strcmp(name, ".") == 0;
}

static bool is_dotdot_name(const char *name)
{
    return strcmp(name, "..") == 0;
}

static void print_indent(uint32_t depth)
{
    uint32_t i;
    for (i = 0; i < depth; i++)
    {
        printf("  ");
    }
}

static bool build_child_path(const char *parent, const char *name, char *out)
{
    uint32_t parent_len;
    uint32_t name_len;
    uint32_t pos;

    if (!parent || !name || !out)
    {
        return false;
    }

    parent_len = strlen(parent);
    name_len = strlen(name);

    if (parent_len == 0 || name_len == 0)
    {
        return false;
    }

    pos = 0;

    if (parent_len == 1 && parent[0] == '/')
    {
        if (1u + name_len + 1u > MAX_PATH_LENGTH)
        {
            return false;
        }

        out[pos++] = '/';

        memcpy(out + pos, name, name_len);
        pos += name_len;
        out[pos] = 0;
        return true;
    }

    if (parent_len + 1u + name_len + 1u > MAX_PATH_LENGTH)
    {
        return false;
    }

    memcpy(out, parent, parent_len);
    pos += parent_len;
    out[pos++] = '/';
    memcpy(out + pos, name, name_len);
    pos += name_len;
    out[pos] = 0;

    return true;
}

static void print_tree_recursive(FS_MOUNT *mount, const char *path, uint32_t depth)
{
    FS_FILE dir;
    FS_DIRENT dir_entry;
    char child_path[MAX_PATH_LENGTH];

    if (depth > MAX_TREE_DEPTH)
    {
        print_indent(depth);
        printf("- max depth reached\r\n");
        return;
    }

    if (!fs_open(mount, path, &dir))
    {
        print_indent(depth);
        printf("- cannot open dir\r\n");
        return;
    }

    if (fs_file_type(&dir) != FS_TYPE_DIR)
    {
        fs_close(&dir);
        return;
    }

    while (fs_read_entry(&dir, &dir_entry))
    {
        if (dir_entry.inode == 0 || dir_entry.name[0] == 0)
        {
            continue;
        }

        if (is_dot_name(dir_entry.name) || is_dotdot_name(dir_entry.name))
        {
            continue;
        }

        if (!build_child_path(path, dir_entry.name, child_path))
        {
            print_indent(depth + 1u);
            printf("- path too long\r\n");
            continue;
        }

        print_indent(depth + 1u);

        if (dir_entry.file_type == FS_TYPE_DIR)
        {
            printf("- %s/\r\n", dir_entry.name);
            print_tree_recursive(mount, child_path, depth + 1u);
            continue;
        }

        if (dir_entry.file_type == FS_TYPE_FILE)
        {
            printf("- %s\r\n", dir_entry.name);
            continue;
        }

        printf("- %s ?\r\n", dir_entry.name);
    }

    fs_close(&dir);
}

void kernel_main(void)
{
    ATA_DEVICE ata;
    FS_MOUNT mount;

    printf("Kernel: entered 32-bit C main\r\n");

    if (!ATA_Initialize(&ata))
    {
        printf("kernel: ATA init failed\r\n");
    }
    else if (!fs_mount(&mount, &ata.block))
    {
        printf("kernel: EXT2 mount failed\r\n");
    }
    else
    {
        printf("EXT2 recursive tree test\r\n");
        printf("/\r\n");
        print_tree_recursive(&mount, "/", 0u);
    }

    for (;;)
        ;
}
