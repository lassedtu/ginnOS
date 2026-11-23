/**
 * @file gfx_blur.c
 * @brief libgfx: a Gaussian blur of a whole buffer (a frosted backdrop).
 *
 * three box blurs in each direction approximate the Gaussian (the SVG
 * feGaussianBlur method, as for the masks of shadows). each channel of
 * the premultiplied pixels is blurred. at the edges the edge pixel is used
 * again, so the edges do not get dark.
 */

#include "gfx.h"

#include <math.h>
#include <stdlib.h>

/**
 * one box blur of n pixels, step apart: each becomes the mean of the size
 * pixels from i - before. tmp holds n pixels.
 */
static void box_blur(uint32_t *line, int32_t n, int32_t step, int32_t size, int32_t before,
                     uint32_t *tmp)
{
    for (int32_t i = 0; i < n; i++)
        tmp[i] = line[(ptrdiff_t)i * step];
    uint32_t sum[4] = {0, 0, 0, 0};
    for (int32_t k = -before; k < -before + size; k++)
    {
        uint32_t p = tmp[k < 0 ? 0 : k >= n ? n - 1 : k];
        for (int c = 0; c < 4; c++)
            sum[c] += (p >> (8 * c)) & 0xff;
    }
    for (int32_t i = 0; i < n; i++)
    {
        uint32_t out = 0;
        for (int c = 0; c < 4; c++)
            out |= ((sum[c] + (uint32_t)size / 2) / (uint32_t)size) << (8 * c);
        line[(ptrdiff_t)i * step] = out;
        int32_t gone = i - before;
        int32_t next = i - before + size;
        uint32_t a = tmp[gone < 0 ? 0 : gone >= n ? n - 1 : gone];
        uint32_t b = tmp[next < 0 ? 0 : next >= n ? n - 1 : next];
        for (int c = 0; c < 4; c++)
            sum[c] = sum[c] - ((a >> (8 * c)) & 0xff) + ((b >> (8 * c)) & 0xff);
    }
}

void gfx_blur(gfx_buffer_t *buf, double sigma)
{
    int32_t d = (int32_t)floor(sigma * 3 * sqrt(2 * M_PI) / 4 + 0.5);
    if (d < 2 || buf->width <= 0 || buf->height <= 0)
        return;
    // an odd size is centred; an even one is used twice, once on each side
    // of the pixel, then once one larger and centred
    int32_t sizes[3] = {d, d, d};
    int32_t befores[3] = {d / 2, d / 2, d / 2};
    if (d % 2 == 0)
    {
        befores[1] = d / 2 - 1;
        sizes[2] = d + 1;
    }
    int32_t longest = buf->width > buf->height ? buf->width : buf->height;
    uint32_t *tmp = malloc((size_t)longest * sizeof(uint32_t));
    if (!tmp)
        return;
    for (int32_t pass = 0; pass < 3; pass++)
    {
        for (int32_t y = 0; y < buf->height; y++)
            box_blur(gfx_pixel_at(buf, 0, y), buf->width, 1, sizes[pass], befores[pass], tmp);
        for (int32_t x = 0; x < buf->width; x++)
            box_blur(gfx_pixel_at(buf, x, 0), buf->height, buf->stride, sizes[pass],
                     befores[pass], tmp);
    }
    free(tmp);
}
