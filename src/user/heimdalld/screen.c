/**
 * @file screen.c
 * @brief the composition: the damaged parts of the screen, drawn again.
 *
 * whatever changes (a window commits, moves, gets the focus; the pointer
 * moves) adds its rectangle to the damage list. screen_compose draws each
 * damaged rectangle again, bottom to top: the wallpaper, the windows, the
 * outline of a resize, the pointer, and copies it to the framebuffer.
 */

#include "heimdalld.h"

#include <fcntl.h>
#include <image.h>
#include <stdio.h>
#include <sys/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * the wallpaper: an image scaled to cover the screen, or a gradient.
 */
static bool make_wallpaper(const char *path)
{
    S.wallpaper = gfx_buffer_new(S.width, S.height);
    if (!S.wallpaper)
        return false;
    gfx_fill_gradient(S.wallpaper, gfx_rect(0, 0, S.width, S.height), gfx_rgb(40, 60, 110),
                      gfx_rgb(120, 70, 130));
    if (!path)
        return true;

    const char *error = NULL;
    gfx_buffer_t *img = image_load(path, &error);
    if (!img)
    {
        printf("heimdalld: %s: %s\n", path, error);
        return true;
    }
    // cover: scale so both sides fill the screen, and cut the rest in the middle
    gfx_rect_t src = gfx_rect(0, 0, img->width, img->height);
    if ((int64_t)img->width * S.height > (int64_t)img->height * S.width)
    {
        src.w = (int32_t)((int64_t)img->height * S.width / S.height);
        src.x = (img->width - src.w) / 2;
    }
    else
    {
        src.h = (int32_t)((int64_t)img->width * S.height / S.width);
        src.y = (img->height - src.h) / 2;
    }
    gfx_blend_scaled(S.wallpaper, gfx_rect(0, 0, S.width, S.height), img, src, 255);
    gfx_buffer_free(img);
    return true;
}

bool screen_init(const char *wallpaper)
{
    int fd = fb_open();
    fb_var_screeninfo_t var;
    fb_fix_screeninfo_t fix;
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
    {
        printf("heimdalld: no framebuffer\n");
        return false;
    }
    int on = 1;
    if (ioctl(fd, FBIO_GRAPHICS, &on) != 0)
    {
        printf("heimdalld: another program has the screen\n");
        close(fd);
        return false;
    }
    S.fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (S.fb == MAP_FAILED)
    {
        printf("heimdalld: cannot map the framebuffer\n");
        return false;
    }
    S.pitch = fix.line_length;
    S.width = (int32_t)var.xres;
    S.height = (int32_t)var.yres;
    S.format.bytes_per_pixel = (uint8_t)(var.bits_per_pixel / 8);
    S.format.red_size = (uint8_t)var.red.length;
    S.format.red_shift = (uint8_t)var.red.offset;
    S.format.green_size = (uint8_t)var.green.length;
    S.format.green_shift = (uint8_t)var.green.offset;
    S.format.blue_size = (uint8_t)var.blue.length;
    S.format.blue_shift = (uint8_t)var.blue.offset;

    S.screen = gfx_buffer_new(S.width, S.height);
    if (!S.screen || !make_wallpaper(wallpaper))
    {
        printf("heimdalld: out of memory\n");
        return false;
    }
    S.px = S.width / 2;
    S.py = S.height / 2;
    S.cursor_rect = cursor_rect(S.cursor, S.px, S.py);
    screen_damage(gfx_rect(0, 0, S.width, S.height));
    return true;
}

void screen_damage(gfx_rect_t r)
{
    r = gfx_rect_intersect(r, gfx_rect(0, 0, S.width, S.height));
    if (gfx_rect_empty(r))
        return;
    // join it with a rectangle it overlaps, so a part is not drawn twice
    for (int i = 0; i < S.damage_count; i++)
    {
        if (!gfx_rect_empty(gfx_rect_intersect(S.damage[i], r)))
        {
            S.damage[i] = gfx_rect_union(S.damage[i], r);
            return;
        }
    }
    if (S.damage_count == DAMAGE_MAX)
    {
        // too many: one rectangle around them all
        for (int i = 1; i < S.damage_count; i++)
            S.damage[0] = gfx_rect_union(S.damage[0], S.damage[i]);
        S.damage[0] = gfx_rect_union(S.damage[0], r);
        S.damage_count = 1;
        return;
    }
    S.damage[S.damage_count++] = r;
}

/**
 * the outline of a resize: a frame two pixels wide.
 */
static void draw_outline(gfx_rect_t r)
{
    gfx_stroke_rect(S.screen, r, 2, gfx_rgba(255, 255, 255, 200));
    gfx_stroke_rect(S.screen, gfx_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4), 1,
                    gfx_rgba(0, 0, 0, 90));
}

static void compose_rect(gfx_rect_t r)
{
    gfx_set_clip(S.screen, r);
    gfx_copy(S.screen, r.x, r.y, S.wallpaper, r);
    for (window_t *w = S.bottom; w; w = w->above)
    {
        if (window_visible(w) &&
            !gfx_rect_empty(gfx_rect_intersect(window_bounds(w), r)))
            window_draw(w, r);
    }
    if (S.drag == DRAG_RESIZE)
        draw_outline(S.drag_rect);
    cursor_draw(S.screen, S.cursor, S.px, S.py);
    gfx_reset_clip(S.screen);
    gfx_present(S.fb, S.pitch, S.width, S.height, &S.format, S.screen, r);
}

void screen_compose(void)
{
    // the pointer: where it was, and where it is now
    gfx_rect_t now = cursor_rect(S.cursor, S.px, S.py);
    if (now.x != S.cursor_rect.x || now.y != S.cursor_rect.y || now.w != S.cursor_rect.w ||
        now.h != S.cursor_rect.h)
    {
        screen_damage(S.cursor_rect);
        screen_damage(now);
        S.cursor_rect = now;
    }

    for (int i = 0; i < S.damage_count; i++)
        compose_rect(S.damage[i]);
    S.damage_count = 0;

    // the commits are on the screen now
    for (window_t *w = S.bottom; w; w = w->above)
    {
        if (w->frame_pending)
        {
            w->frame_pending = false;
            heim_msg_t msg = {0};
            msg.type = HEIM_EV_FRAME;
            client_send_window(w, &msg);
        }
    }
}
