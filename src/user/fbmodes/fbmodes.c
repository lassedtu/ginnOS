/**
 * @file fbmodes.c
 * @brief list the video modes the firmware offers.
 *
 * usage: fbmodes
 *
 * prints one mode per line (width x height and bits per pixel), with a *
 * at the mode in use. the mode is chosen at boot, in limine.conf.
 */

#include <stdio.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    int fd = fb_open();
    if (fd < 0)
    {
        printf("fbmodes: no framebuffer\n");
        return 1;
    }
    for (uint32_t i = 0;; i++)
    {
        fb_mode_t mode = {.index = i};
        if (ioctl(fd, FBIOGET_MODE, &mode) != 0)
        {
            break;
        }
        printf("%c %ux%u, %u bpp\n", mode.current ? '*' : ' ', mode.width, mode.height,
               mode.bpp);
    }
    close(fd);
    return 0;
}
