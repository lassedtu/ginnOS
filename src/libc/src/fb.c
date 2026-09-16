#include "fb.h"
#include "syscall.h"
#include "errno.h"

/**
 * @file fb.c
 * @brief userspace framebuffer syscall wrappers.
 */

int fbinfo(fb_info_t *info)
{
    int ret = _syscall(SYS_FBINFO, (int)info, 0, 0, 0, 0);
    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return ret;
}

void *fbmap(void)
{
    int ret = _syscall(SYS_FBMAP, 0, 0, 0, 0, 0);
    if (ret == 0)
    {
        return (void *)0; // NULL: no framebuffer / map failed
    }
    return (void *)ret;
}

int pollkey(void)
{
    return _syscall(SYS_POLLKEY, 0, 0, 0, 0, 0);
}
