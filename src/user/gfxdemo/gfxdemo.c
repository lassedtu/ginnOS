/**
 * @file gfxdemo.c
 * @brief draw a mock desktop with libgfx on /dev/fb0.
 *
 * usage: gfxdemo [image]
 *
 * with an image (PNG, JPEG or BMP), it is the wallpaper, scaled to the
 * screen.
 *
 * draws into a buffer in memory, then copies it to the screen in one
 * step (gfx_present), and waits for Enter. a test of libgfx on the real
 * framebuffer, until the compositor exists.
 */

#include <gfx.h>
#include <image.h>
#include <stdio.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * a window: a soft shadow, the body, a title bar and three buttons.
 */
static void draw_window(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t title)
{
    // the shadow: rings of faint black, wider and fainter outwards
    for (int32_t i = 12; i > 0; i--)
    {
        gfx_rect_t s = gfx_rect(r.x - i, r.y - i + 6, r.w + 2 * i, r.h + 2 * i);
        gfx_fill_round_rect(buf, s, 10 + i, gfx_rgba(0, 0, 0, 6));
    }
    gfx_fill_round_rect(buf, r, 10, gfx_rgb(245, 245, 248));

    // the title bar: round at the top only (the lower corners are covered)
    gfx_fill_round_rect(buf, gfx_rect(r.x, r.y, r.w, 36), 10, title);
    gfx_fill_rect(buf, gfx_rect(r.x, r.y + 26, r.w, 10), title);
    gfx_line(buf, r.x, r.y + 36, r.x + r.w - 1, r.y + 36, gfx_rgba(0, 0, 0, 60));

    // minimize, maximize, close
    static const uint8_t colors[3][3] = {{250, 190, 60}, {80, 200, 110}, {240, 90, 80}};
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t x = r.x + r.w - 26 - (2 - i) * 24;
        gfx_fill_round_rect(buf, gfx_rect(x, r.y + 11, 14, 14), 7,
                            gfx_rgb(colors[i][0], colors[i][1], colors[i][2]));
    }

    // some content: rows of grey bars
    for (int32_t row = 0; row < 6; row++)
    {
        int32_t width = (r.w - 40) * (10 - row) / 10;
        gfx_fill_round_rect(buf, gfx_rect(r.x + 20, r.y + 56 + row * 26, width, 12), 6,
                            gfx_rgb(215, 218, 226));
    }
}

int main(int argc, char **argv)
{
    int fd = fb_open();
    fb_var_screeninfo_t var;
    fb_fix_screeninfo_t fix;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        printf("gfxdemo: no framebuffer\n");
        return 1;
    }
    void *fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (fb == MAP_FAILED)
    {
        printf("gfxdemo: cannot map the framebuffer\n");
        return 1;
    }
    gfx_format_t format = {
        (uint8_t)(var.bits_per_pixel / 8), (uint8_t)var.red.length,  (uint8_t)var.red.offset,
        (uint8_t)var.green.length,         (uint8_t)var.green.offset, (uint8_t)var.blue.length,
        (uint8_t)var.blue.offset,
    };

    int32_t w = (int32_t)var.xres;
    int32_t h = (int32_t)var.yres;
    gfx_buffer_t *screen = gfx_buffer_new(w, h);
    if (!screen)
    {
        printf("gfxdemo: out of memory\n");
        return 1;
    }

    // the wallpaper, and a panel at the bottom
    gfx_fill_gradient(screen, gfx_rect(0, 0, w, h), gfx_rgb(40, 60, 110), gfx_rgb(120, 70, 130));
    if (argc > 1)
    {
        const char *error = NULL;
        gfx_buffer_t *wallpaper = image_load(argv[1], &error);
        if (!wallpaper)
        {
            printf("gfxdemo: %s: %s\n", argv[1], error);
            return 1;
        }
        gfx_blend_scaled(screen, gfx_rect(0, 0, w, h), wallpaper,
                         gfx_rect(0, 0, wallpaper->width, wallpaper->height), 255);
        gfx_buffer_free(wallpaper);
    }
    gfx_fill_rect(screen, gfx_rect(0, h - 40, w, 40), gfx_rgba(10, 10, 20, 170));
    for (int32_t i = 0; i < 5; i++)
        gfx_fill_round_rect(screen, gfx_rect(12 + i * 40, h - 32, 28, 24), 6,
                            gfx_rgba(255, 255, 255, 60 + i * 30));

    draw_window(screen, gfx_rect(w / 10, h / 8, w / 2, h / 2), gfx_rgb(225, 228, 236));
    draw_window(screen, gfx_rect(w * 4 / 10, h * 3 / 8, w / 2, h / 2), gfx_rgb(200, 215, 245));

    gfx_present(fb, fix.line_length, w, h, &format, screen, gfx_rect(0, 0, w, h));

    char c;
    while (read(0, &c, 1) == 1 && c != '\n')
        ;
    gfx_buffer_free(screen);
    printf("\033[2J\033[H");
    return 0;
}
