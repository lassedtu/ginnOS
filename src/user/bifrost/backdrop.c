/**
 * @file backdrop.c
 * @brief Bifrost: the picture under the login screen: the wallpaper,
 *        blurred and darkened (frosted glass).
 *
 * the blur is done on a picture a quarter of the size, then scaled up:
 * this is faster, and the bilinear scale makes it smoother still.
 */

#include "bifrost.h"

#include <conf.h>
#include <image.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_WALLPAPER "/usr/share/wallpapers/default.png"
#define SMALL             4  // the blur is done at 1/SMALL of the size
#define SIGMA             10 // of the blur, in pixels of the small picture
#define DARKEN            90 // the black over it, of 255

/** the wallpaper of the settings (as heimdalld finds it), or NULL for none */
static const char *wallpaper_path(char *path, size_t length)
{
    conf_t *conf = conf_open("desktop");
    const char *value = conf ? conf_get(conf, "desktop", "wallpaper", NULL) : NULL;
    const char *result = access(DEFAULT_WALLPAPER, R_OK) == 0 ? DEFAULT_WALLPAPER : NULL;
    if (value && strcmp(value, "none") == 0)
        result = NULL;
    else if (value && value[0])
    {
        snprintf(path, length, "%s", value);
        result = path;
    }
    conf_free(conf);
    return result;
}

/** draw img over all of dst, scaled to cover it (the rest cut off in the middle) */
static void cover(gfx_buffer_t *dst, const gfx_buffer_t *img)
{
    gfx_rect_t src = gfx_rect(0, 0, img->width, img->height);
    if ((int64_t)img->width * dst->height > (int64_t)img->height * dst->width)
    {
        src.w = (int32_t)((int64_t)img->height * dst->width / dst->height);
        src.x = (img->width - src.w) / 2;
    }
    else
    {
        src.h = (int32_t)((int64_t)img->width * dst->height / dst->width);
        src.y = (img->height - src.h) / 2;
    }
    gfx_blend_scaled(dst, gfx_rect(0, 0, dst->width, dst->height), img, src, 255);
}

gfx_buffer_t *backdrop_make(int32_t width, int32_t height)
{
    int32_t small_w = width / SMALL > 1 ? width / SMALL : 1;
    int32_t small_h = height / SMALL > 1 ? height / SMALL : 1;
    gfx_buffer_t *small = gfx_buffer_new(small_w, small_h);
    gfx_buffer_t *out = gfx_buffer_new(width, height);
    if (!small || !out)
    {
        gfx_buffer_free(small);
        gfx_buffer_free(out);
        return NULL;
    }
    // the gradient heimdalld shows when there is no wallpaper
    gfx_fill_gradient(small, gfx_rect(0, 0, small_w, small_h), gfx_rgb(40, 60, 110),
                      gfx_rgb(120, 70, 130));
    char path[256];
    const char *file = wallpaper_path(path, sizeof(path));
    const char *error = NULL;
    gfx_buffer_t *img = file ? image_load(file, &error) : NULL;
    if (img)
    {
        cover(small, img);
        gfx_buffer_free(img);
    }
    gfx_blur(small, SIGMA);
    gfx_fill_rect(small, gfx_rect(0, 0, small_w, small_h), gfx_rgba(0, 0, 0, DARKEN));
    gfx_clear(out, gfx_rgb(0, 0, 0));
    gfx_blend_scaled(out, gfx_rect(0, 0, width, height), small, gfx_rect(0, 0, small_w, small_h),
                     255);
    gfx_buffer_free(small);
    return out;
}
