#pragma once

/**
 * @file icon.h
 * @brief icons from the icon packs in /usr/share/icons.
 *
 * two sets: "lucide" has line icons as coverage masks, drawn in a colour
 * given at draw time (window controls, toolbars); "tela" has full colour
 * icons (apps, places, file types, devices). tools/icons/build.py makes
 * the packs; the format is described there.
 *
 * an icon is loaded at a size and a scale (1, or 2 for a 2x screen). when
 * the pack has no image of that many pixels, the next larger one is
 * scaled down (or the largest one up).
 */

#include <gfx.h>
#include <stdbool.h>
#include <stdint.h>

#define ICON_DIR "/usr/share/icons/"
#define ICON_NAME_MAX 31

typedef struct icon_set icon_set_t;

typedef struct
{
    int32_t width;
    int32_t height;
    bool is_mask;
    uint8_t *mask;       // is_mask: width * height coverage bytes
    gfx_buffer_t *image; // else: premultiplied ARGB
} icon_t;

/**
 * open a pack file.
 * @return the set, or NULL (no file, or not an icon pack).
 */
icon_set_t *icon_set_open(const char *path);

/**
 * open a set by its name: ICON_DIR "<name>.pack".
 */
icon_set_t *icon_set_named(const char *name);

void icon_set_close(icon_set_t *set);

/**
 * true if the set is of masks (lucide), false if of colour images (tela).
 */
bool icon_set_is_mask(const icon_set_t *set);

/**
 * true if the set has an icon of exactly this name.
 */
bool icon_set_has(const icon_set_t *set, const char *name);

/**
 * load an icon.
 *
 * a name the set does not have falls back as in the freedesktop icon
 * naming spec: the last part after a dash is removed until a name is
 * found (text-x-csrc, text-x, text), then "unknown".
 *
 * @param size the icon size in points (16, 20, 24, 32, 48, 64, or any).
 * @param scale 1 to 4 (2 for a 2x screen); the icon is size * scale
 *        pixels, at most 1024.
 * @return the icon (free it with icon_free), or NULL.
 */
icon_t *icon_load(icon_set_t *set, const char *name, int32_t size, int32_t scale);

void icon_free(icon_t *icon);

/**
 * draw an icon in a colour: a mask's coverage, or a colour image's alpha.
 */
void icon_draw_mask(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y,
                    gfx_color_t color);

/**
 * draw an icon as it is, with an opacity (255: opaque). a mask is drawn
 * in black.
 */
void icon_draw(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y, uint8_t opacity);
