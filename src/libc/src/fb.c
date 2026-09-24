#include "fb.h"

#include <sys/fb.h>
#include <sys/mman.h>
#include <unistd.h>

#include "syscall.h"
#include "errno.h"

/**
 * @file fb.c
 * @brief userspace framebuffer wrappers over the /dev/fb0 fbdev.
 *
 * fbinfo()/fbmap() are kept for source compatibility with the earlier
 * interim interface, but are now implemented on top of the Linux-style
 * framebuffer device: open("/dev/fb0"), ioctl for geometry, mmap for pixels.
 */

int fb_open(void)
{
    return open("/dev/fb0", 0);
}

int fbinfo(fb_info_t *info)
{
    int fd = fb_open();
    if (fd < 0)
    {
        return -1;
    }

    fb_var_screeninfo_t var;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0)
    {
        close(fd);
        return -1;
    }

    fb_fix_screeninfo_t fix;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        close(fd);
        return -1;
    }

    info->width = var.xres;
    info->height = var.yres;
    info->pitch = fix.line_length;
    info->bpp = var.bits_per_pixel;
    info->red_size = (unsigned char)var.red.length;
    info->red_shift = (unsigned char)var.red.offset;
    info->green_size = (unsigned char)var.green.length;
    info->green_shift = (unsigned char)var.green.offset;
    info->blue_size = (unsigned char)var.blue.length;
    info->blue_shift = (unsigned char)var.blue.offset;
    info->_pad[0] = 0;
    info->_pad[1] = 0;

    close(fd);
    return 0;
}

void *fbmap(void)
{
    int fd = fb_open();
    if (fd < 0)
    {
        return (void *)0;
    }

    fb_fix_screeninfo_t fix;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        close(fd);
        return (void *)0;
    }

    // the kernel maps the framebuffer eagerly into the page directory, so the
    // mapping survives closing the fd. return NULL on failure for the old ABI.
    void *p = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (p == MAP_FAILED)
    {
        return (void *)0;
    }
    return p;
}

int pollkey(void)
{
    return _syscall(SYS_POLLKEY, 0, 0, 0, 0, 0);
}
