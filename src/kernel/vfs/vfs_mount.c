#include "vfs.h"

FS_MOUNT *root_mount = 0;

kerr_t vfs_mount_root(FS_MOUNT *mount)
{
    if (!mount || !mount->is_mounted)
    {
        return KERR_INVAL;
    }

    root_mount = mount;

    return KERR_OK;
}
