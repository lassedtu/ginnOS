/**
 * @file icon.c
 * @brief icons from the icon packs (the format is in tools/icons/build.py).
 *
 * the index of a pack is read when it is opened; an image is read from
 * the file and decompressed (zlib, through the image library) when it is
 * loaded.
 */

#include "icon.h"

#include <fcntl.h>
#include <image.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADER_SIZE 16
#define ENTRY_SIZE 48
#define COUNT_MAX 65536
#define SIDE_MAX 1024

typedef struct
{
    char name[ICON_NAME_MAX + 1];
    uint16_t size;
    uint8_t scale;
    uint16_t width;
    uint16_t height;
    uint32_t offset;
    uint32_t length;
} entry_t;

struct icon_set
{
    int fd;
    bool is_mask;
    uint32_t count;
    entry_t *entries; // sorted by name, size, scale
};

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool read_at(int fd, void *buf, size_t size, uint32_t offset)
{
    size_t done = 0;
    while (done < size)
    {
        ssize_t n = pread(fd, (uint8_t *)buf + done, size - done, (int)(offset + done));
        if (n <= 0)
            return false;
        done += (size_t)n;
    }
    return true;
}

icon_set_t *icon_set_open(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    uint8_t header[HEADER_SIZE];
    uint8_t *index = NULL;
    icon_set_t *set = NULL;
    if (!read_at(fd, header, sizeof(header), 0) || memcmp(header, "GIPK", 4) != 0 ||
        get16(header + 4) != 1 || header[6] > 1)
        goto fail;
    uint32_t count = get32(header + 8);
    if (count == 0 || count > COUNT_MAX)
        goto fail;

    index = malloc((size_t)count * ENTRY_SIZE);
    set = calloc(1, sizeof(*set));
    if (!index || !set || !read_at(fd, index, (size_t)count * ENTRY_SIZE, HEADER_SIZE))
        goto fail;
    set->entries = calloc(count, sizeof(entry_t));
    if (!set->entries)
        goto fail;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *e = index + (size_t)i * ENTRY_SIZE;
        entry_t *out = &set->entries[i];
        memcpy(out->name, e, ICON_NAME_MAX);
        out->name[ICON_NAME_MAX] = '\0';
        out->size = get16(e + 32);
        out->scale = e[34];
        out->width = get16(e + 36);
        out->height = get16(e + 38);
        out->offset = get32(e + 40);
        out->length = get32(e + 44);
        if (out->width == 0 || out->height == 0 || out->width > SIDE_MAX ||
            out->height > SIDE_MAX)
            goto fail;
    }
    free(index);
    set->fd = fd;
    set->is_mask = header[6] == 0;
    set->count = count;
    return set;

fail:
    free(index);
    if (set)
        free(set->entries);
    free(set);
    close(fd);
    return NULL;
}

icon_set_t *icon_set_named(const char *name)
{
    char path[128];
    size_t dir = strlen(ICON_DIR);
    size_t len = strlen(name);
    if (dir + len + 6 > sizeof(path))
        return NULL;
    memcpy(path, ICON_DIR, dir);
    memcpy(path + dir, name, len);
    memcpy(path + dir + len, ".pack", 6);
    return icon_set_open(path);
}

void icon_set_close(icon_set_t *set)
{
    if (!set)
        return;
    close(set->fd);
    free(set->entries);
    free(set);
}

bool icon_set_is_mask(const icon_set_t *set)
{
    return set->is_mask;
}

/**
 * the first entry of a name (binary search), or -1.
 */
static int32_t find(const icon_set_t *set, const char *name)
{
    int32_t lo = 0;
    int32_t hi = (int32_t)set->count;
    while (lo < hi)
    {
        int32_t mid = (lo + hi) / 2;
        if (strcmp(set->entries[mid].name, name) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < (int32_t)set->count && strcmp(set->entries[lo].name, name) == 0 ? lo : -1;
}

bool icon_set_has(const icon_set_t *set, const char *name)
{
    return find(set, name) >= 0;
}

/**
 * the entry for a name at a number of pixels: exact, else the smallest
 * larger one, else the largest. an exact size and scale wins over the
 * same pixels from another size (Lucide has thinner lines when small).
 */
static const entry_t *choose(const icon_set_t *set, int32_t first, int32_t size, int32_t scale)
{
    int32_t pixels = size * scale;
    const entry_t *best = NULL;
    for (int32_t i = first; i < (int32_t)set->count; i++)
    {
        const entry_t *e = &set->entries[i];
        if (strcmp(e->name, set->entries[first].name) != 0)
            break;
        if (e->size == size && e->scale == scale)
            return e;
        int32_t have = e->width;
        int32_t best_have = best ? best->width : 0;
        if (!best || (have >= pixels && (best_have < pixels || have < best_have)) ||
            (have < pixels && best_have < pixels && have > best_have) ||
            (have == best_have && e->scale == scale))
            best = e;
    }
    return best;
}

/**
 * read and decompress one image: width * height bytes or pixels.
 */
static uint8_t *read_image(const icon_set_t *set, const entry_t *e)
{
    size_t bytes = (size_t)e->width * e->height * (set->is_mask ? 1 : 4);
    uint8_t *packed = malloc(e->length ? e->length : 1);
    uint8_t *raw = malloc(bytes);
    if (!packed || !raw || !read_at(set->fd, packed, e->length, e->offset) ||
        image_inflate(packed, e->length, raw, bytes) != (int32_t)bytes)
    {
        free(packed);
        free(raw);
        return NULL;
    }
    free(packed);
    return raw;
}

/**
 * a premultiplied buffer of an image's pixels (a mask as black with alpha).
 */
static gfx_buffer_t *to_buffer(const icon_set_t *set, const entry_t *e, const uint8_t *raw)
{
    gfx_buffer_t *buf = gfx_buffer_new(e->width, e->height);
    if (!buf)
        return NULL;
    for (int32_t i = 0; i < e->width * e->height; i++)
        buf->pixels[i] = set->is_mask ? (uint32_t)raw[i] << 24
                                      : (uint32_t)raw[i * 4] | (uint32_t)raw[i * 4 + 1] << 8 |
                                            (uint32_t)raw[i * 4 + 2] << 16 |
                                            (uint32_t)raw[i * 4 + 3] << 24;
    return buf;
}

static icon_t *make_icon(const icon_set_t *set, const entry_t *e, int32_t pixels)
{
    uint8_t *raw = read_image(set, e);
    if (!raw)
        return NULL;
    icon_t *icon = calloc(1, sizeof(*icon));
    if (!icon)
    {
        free(raw);
        return NULL;
    }
    icon->is_mask = set->is_mask;

    if (e->width == pixels)
    {
        icon->width = e->width;
        icon->height = e->height;
        if (set->is_mask)
        {
            icon->mask = raw;
            return icon;
        }
        icon->image = to_buffer(set, e, raw);
        free(raw);
        if (!icon->image)
        {
            free(icon);
            return NULL;
        }
        return icon;
    }

    // scale to the size asked for (the icons are square)
    gfx_buffer_t *src = to_buffer(set, e, raw);
    free(raw);
    int32_t height = e->height * pixels / e->width;
    gfx_buffer_t *dst = src ? gfx_buffer_new(pixels, height) : NULL;
    if (!dst)
    {
        gfx_buffer_free(src);
        free(icon);
        return NULL;
    }
    gfx_blend_scaled(dst, gfx_rect(0, 0, pixels, height), src,
                     gfx_rect(0, 0, e->width, e->height), 255);
    gfx_buffer_free(src);
    icon->width = pixels;
    icon->height = height;
    if (!set->is_mask)
    {
        icon->image = dst;
        return icon;
    }
    icon->mask = malloc((size_t)pixels * height);
    if (icon->mask)
        for (int32_t i = 0; i < pixels * height; i++)
            icon->mask[i] = gfx_alpha(dst->pixels[i]);
    gfx_buffer_free(dst);
    if (!icon->mask)
    {
        free(icon);
        return NULL;
    }
    return icon;
}

icon_t *icon_load(icon_set_t *set, const char *name, int32_t size, int32_t scale)
{
    if (!set || !name || size <= 0 || scale < 1 || scale > 4 || size * scale > SIDE_MAX)
        return NULL;
    char try[ICON_NAME_MAX + 1];
    size_t len = strlen(name);
    if (len > ICON_NAME_MAX)
        len = ICON_NAME_MAX;
    memcpy(try, name, len);
    try[len] = '\0';

    for (;;)
    {
        int32_t first = find(set, try);
        if (first >= 0)
            return make_icon(set, choose(set, first, size, scale), size * scale);
        char *dash = strrchr(try, '-');
        if (!dash)
            break;
        *dash = '\0';
    }
    int32_t first = find(set, "unknown");
    return first >= 0 ? make_icon(set, choose(set, first, size, scale), size * scale) : NULL;
}

void icon_free(icon_t *icon)
{
    if (!icon)
        return;
    free(icon->mask);
    gfx_buffer_free(icon->image);
    free(icon);
}

void icon_draw_mask(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y,
                    gfx_color_t color)
{
    if (icon->is_mask)
    {
        gfx_draw_mask(buf, x, y, icon->mask, icon->width, icon->height, icon->width, color);
        return;
    }
    // a colour image: its alpha as the mask, one row at a time
    uint8_t row[SIDE_MAX];
    for (int32_t r = 0; r < icon->height; r++)
    {
        const uint32_t *p = gfx_pixel_at(icon->image, 0, r);
        for (int32_t c = 0; c < icon->width; c++)
            row[c] = gfx_alpha(p[c]);
        gfx_draw_mask(buf, x, y + r, row, icon->width, 1, icon->width, color);
    }
}

void icon_draw(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y, uint8_t opacity)
{
    if (icon->is_mask)
    {
        gfx_draw_mask(buf, x, y, icon->mask, icon->width, icon->height, icon->width,
                      gfx_fade(gfx_rgb(0, 0, 0), opacity));
        return;
    }
    gfx_blend(buf, x, y, icon->image, gfx_rect(0, 0, icon->width, icon->height), opacity);
}
