/**
 * @file display.c
 * @brief list the screen modes, or change the mode.
 *
 * usage: display            list the modes: * the one in use, + the size
 *                           the screen wants (the QEMU window)
 *        display WxH        change the mode, for example display 1920x1080
 *
 * a mode can only be changed with a display driver (virtio-gpu); without
 * one the list is the firmware's, and the mode is chosen at boot
 * (limine.conf). the window server follows a change by itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int list(int fd)
{
    for (uint32_t i = 0;; i++)
    {
        fb_mode_t mode = {.index = i};
        if (ioctl(fd, FBIOGET_MODE, &mode) != 0)
        {
            break;
        }
        printf("%c%c %ux%u, %u bpp\n", mode.current ? '*' : ' ', mode.preferred ? '+' : ' ',
               mode.width, mode.height, mode.bpp);
    }
    return 0;
}

static int set(int fd, const char *size)
{
    char *end;
    unsigned long width = strtoul(size, &end, 10);
    unsigned long height = *end == 'x' ? strtoul(end + 1, &end, 10) : 0;
    if (width == 0 || height == 0 || *end != '\0')
    {
        printf("display: %s is not a size like 1920x1080\n", size);
        return 2;
    }
    fb_var_screeninfo_t var = {.xres = (uint32_t)width,
                               .yres = (uint32_t)height,
                               .bits_per_pixel = 32};
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) != 0)
    {
        printf("display: cannot set %lux%lu\n", width, height);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        printf("usage: display [WxH]\n");
        return 2;
    }
    int fd = fb_open();
    if (fd < 0)
    {
        printf("display: no framebuffer\n");
        return 1;
    }
    int status = argc == 2 ? set(fd, argv[1]) : list(fd);
    close(fd);
    return status;
}
