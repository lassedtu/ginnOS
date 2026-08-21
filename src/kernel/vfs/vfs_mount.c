#include "vfs.h"

fs_mount_t *root_mount = NULL;

kerr_t vfs_mount_root(fs_mount_t *mount)
{
    if (!mount || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    root_mount = mount;

    return KERR_OK;
}
