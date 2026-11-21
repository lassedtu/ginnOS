/**
 * @file gfx_test.c
 * @brief host tests of libgfx (make host-test).
 */

#include "gfx.h"
#include "../gfx_internal.h"
#include "hosttest.h"

#include <stdlib.h>
#include <string.h>

/**
 * compare a buffer with its golden image (the pixels over black).
 */
static void golden(const char *name, const gfx_buffer_t *buf)
{
    uint8_t *rgb = malloc((size_t)buf->width * buf->height * 3);
    for (int32_t y = 0; y < buf->height; y++)
    {
        for (int32_t x = 0; x < buf->width; x++)
        {
            uint32_t p = *gfx_pixel_at(buf, x, y);
            uint8_t *out = rgb + ((size_t)y * buf->width + x) * 3;
            out[0] = (uint8_t)(p >> 16);
            out[1] = (uint8_t)(p >> 8);
            out[2] = (uint8_t)p;
        }
    }
    hosttest_golden(name, rgb, buf->width, buf->height);
    free(rgb);
}

static void test_arithmetic(void)
{
    // every channel value times every alpha, against exact rounding
    int wrong = 0;
    for (uint32_t v = 0; v < 256; v++)
    {
        for (uint32_t a = 0; a < 256; a++)
        {
            uint32_t want = (v * a * 2 + 255) / 510;
            uint32_t got = gfx_scale_pixel(v * 0x01010101u, a);
            if (got != want * 0x01010101u)
                wrong++;
        }
    }
    CHECK_EQ(wrong, 0);

    CHECK_EQ(gfx_rgb(1, 2, 3), 0xFF010203u);
    CHECK_EQ(gfx_rgba(255, 0, 0, 128), 0x80800000u);
    CHECK_EQ(gfx_rgba(10, 20, 30, 0), 0u);
    CHECK_EQ(gfx_fade(gfx_rgb(255, 255, 255), 0), 0u);
    // half white over black is middle grey, opaque
    CHECK_EQ(gfx_over(gfx_rgb(0, 0, 0), gfx_rgba(255, 255, 255, 128)), 0xFF808080u);
    CHECK_EQ(gfx_over(0x12345678u, 0), 0x12345678u);
    CHECK_EQ(gfx_over(0x12345678u, gfx_rgb(9, 9, 9)), gfx_rgb(9, 9, 9));
    // transparent over transparent stays transparent
    CHECK_EQ(gfx_over(0, gfx_rgba(255, 0, 0, 100)), gfx_rgba(255, 0, 0, 100));
    // mixing opaque colours stays opaque
    CHECK_EQ(gfx_lerp_pixel(gfx_rgb(0, 0, 0), gfx_rgb(255, 255, 255), 256), gfx_rgb(255, 255, 255));
    CHECK_EQ(gfx_lerp_pixel(gfx_rgb(10, 20, 30), gfx_rgb(200, 100, 0), 0), gfx_rgb(10, 20, 30));
}

static void test_rects(void)
{
    gfx_rect_t a = gfx_rect(0, 0, 10, 10);
    gfx_rect_t b = gfx_rect(5, -5, 10, 10);
    gfx_rect_t i = gfx_rect_intersect(a, b);
    CHECK(i.x == 5 && i.y == 0 && i.w == 5 && i.h == 5);
    gfx_rect_t u = gfx_rect_union(a, b);
    CHECK(u.x == 0 && u.y == -5 && u.w == 15 && u.h == 15);
    CHECK(gfx_rect_empty(gfx_rect_intersect(a, gfx_rect(10, 0, 5, 5)))); // touching only
    gfx_rect_t e = gfx_rect_union(gfx_rect(0, 0, 0, 0), b);
    CHECK(e.x == b.x && e.w == b.w);
    CHECK(gfx_rect_contains(a, 9, 9) && !gfx_rect_contains(a, 10, 9));
}

static void test_clipping(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(8, 8);
    CHECK(buf != NULL);
    CHECK(gfx_buffer_new(0, 5) == NULL);

    // all outside, or partly: only the inside changes (ASan sees the rest)
    gfx_fill_rect(buf, gfx_rect(-100, -100, 50, 50), gfx_rgb(255, 0, 0));
    gfx_fill_rect(buf, gfx_rect(6, 6, 100, 100), gfx_rgb(255, 0, 0));
    gfx_line(buf, -20, -20, 30, 30, gfx_rgb(0, 255, 0));
    gfx_fill_round_rect(buf, gfx_rect(-5, -5, 40, 40), 10, gfx_rgb(0, 0, 255));
    uint8_t mask[16 * 16];
    memset(mask, 255, sizeof(mask));
    gfx_draw_mask(buf, -4, 5, mask, 16, 16, 16, gfx_rgb(1, 1, 1));

    // the clip rectangle
    gfx_clear(buf, 0);
    gfx_set_clip(buf, gfx_rect(2, 2, 2, 2));
    gfx_fill_rect(buf, gfx_rect(0, 0, 8, 8), gfx_rgb(255, 255, 255));
    int set = 0;
    for (int i = 0; i < 64; i++)
        set += buf->pixels[i] != 0;
    CHECK_EQ(set, 4);
    CHECK_EQ(*gfx_pixel_at(buf, 2, 2), gfx_rgb(255, 255, 255));
    gfx_reset_clip(buf);

    // a sub-buffer draws into its parent's pixels, clipped to its part
    gfx_clear(buf, 0);
    gfx_buffer_t sub = gfx_buffer_sub(buf, gfx_rect(4, 4, 10, 10));
    CHECK(sub.width == 4 && sub.height == 4);
    gfx_fill_rect(&sub, gfx_rect(0, 0, 100, 100), gfx_rgb(9, 9, 9));
    CHECK_EQ(*gfx_pixel_at(buf, 4, 4), gfx_rgb(9, 9, 9));
    CHECK_EQ(*gfx_pixel_at(buf, 3, 4), 0u);
    gfx_buffer_free(buf);
}

static void test_copy_overlap(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(4, 6);
    for (int32_t y = 0; y < 6; y++)
        for (int32_t x = 0; x < 4; x++)
            *gfx_pixel_at(buf, x, y) = (uint32_t)(y * 10 + x);

    // rows 0..3 down by two: rows 2..5 get the old rows 0..3
    gfx_copy(buf, 0, 2, buf, gfx_rect(0, 0, 4, 4));
    CHECK_EQ(*gfx_pixel_at(buf, 1, 5), 31u);
    CHECK_EQ(*gfx_pixel_at(buf, 1, 2), 1u);
    // and up again, one pixel to the right too
    gfx_copy(buf, 1, 0, buf, gfx_rect(0, 2, 3, 4));
    CHECK_EQ(*gfx_pixel_at(buf, 1, 0), 0u);
    CHECK_EQ(*gfx_pixel_at(buf, 3, 3), 32u);
    gfx_buffer_free(buf);
}

static void test_present(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(2, 1);
    buf->pixels[0] = gfx_rgb(0x12, 0x34, 0x56);
    buf->pixels[1] = gfx_rgb(0xFF, 0x80, 0x00);

    uint32_t fb32[2] = {0, 0};
    gfx_format_t xrgb = {4, 8, 16, 8, 8, 8, 0};
    gfx_present(fb32, 8, 2, 1, &xrgb, buf, gfx_rect(0, 0, 2, 1));
    CHECK_EQ(fb32[0] & 0xFFFFFF, 0x123456u);

    uint8_t fb24[6] = {0};
    gfx_format_t bgr24 = {3, 8, 16, 8, 8, 8, 0};
    gfx_present(fb24, 6, 2, 1, &bgr24, buf, gfx_rect(0, 0, 2, 1));
    CHECK(fb24[0] == 0x56 && fb24[1] == 0x34 && fb24[2] == 0x12 && fb24[3] == 0x00);

    uint16_t fb16[2] = {0, 0};
    gfx_format_t rgb565 = {2, 5, 11, 6, 5, 5, 0};
    gfx_present(fb16, 4, 2, 1, &rgb565, buf, gfx_rect(0, 0, 2, 1));
    CHECK_EQ(fb16[1], (0x1F << 11) | (0x20 << 5) | 0);

    // clipped to the framebuffer: only one pixel fits
    uint32_t small[1] = {0};
    gfx_present(small, 4, 1, 1, &xrgb, buf, gfx_rect(0, 0, 2, 1));
    CHECK_EQ(small[0] & 0xFFFFFF, 0x123456u);
    gfx_buffer_free(buf);
}

/**
 * one picture with every kind of drawing, compared with its golden image.
 */
static void test_scene(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(200, 150);
    gfx_fill_gradient(buf, gfx_rect(0, 0, 200, 150), gfx_rgb(30, 40, 70), gfx_rgb(90, 50, 110));

    // filled and stroked rectangles, opaque and see-through
    gfx_fill_rect(buf, gfx_rect(10, 10, 50, 30), gfx_rgb(220, 60, 60));
    gfx_fill_rect(buf, gfx_rect(35, 25, 50, 30), gfx_rgba(60, 200, 90, 140));
    gfx_stroke_rect(buf, gfx_rect(95, 10, 40, 30), 3, gfx_rgba(255, 255, 255, 200));

    // round rectangles: a big radius, a small one, and one made smaller
    gfx_fill_round_rect(buf, gfx_rect(145, 10, 45, 45), 12, gfx_rgb(250, 200, 60));
    gfx_fill_round_rect(buf, gfx_rect(10, 65, 60, 24), 6, gfx_rgba(255, 255, 255, 180));
    gfx_fill_round_rect(buf, gfx_rect(80, 62, 30, 30), 100, gfx_rgb(80, 160, 255));

    // lines in every direction from one centre
    for (int32_t i = 0; i < 16; i++)
    {
        static const int8_t dx[16] = {20, 18, 14, 7, 0, -7, -14, -18, -20, -18, -14, -7, 0, 7, 14, 18};
        static const int8_t dy[16] = {0, 7, 14, 18, 20, 18, 14, 7, 0, -7, -14, -18, -20, -18, -14, -7};
        gfx_line(buf, 150, 90, 150 + dx[i], 90 + dy[i], gfx_rgb(255, 255, 255));
    }

    // a coverage mask: a soft disc
    uint8_t mask[24 * 24];
    for (int y = 0; y < 24; y++)
    {
        for (int x = 0; x < 24; x++)
        {
            int d2 = (x - 12) * (x - 12) + (y - 12) * (y - 12);
            mask[y * 24 + x] = d2 >= 144 ? 0 : (uint8_t)(255 - d2 * 255 / 144);
        }
    }
    gfx_draw_mask(buf, 12, 100, mask, 24, 24, 24, gfx_rgb(255, 220, 120));

    // a 4 x 4 checker, scaled up and down, and blended at half opacity
    gfx_buffer_t *checker = gfx_buffer_new(4, 4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            *gfx_pixel_at(checker, x, y) = (x + y) % 2 ? gfx_rgb(255, 255, 255) : gfx_rgb(0, 0, 0);
    gfx_blend_scaled(buf, gfx_rect(45, 100, 40, 40), checker, gfx_rect(0, 0, 4, 4), 255);
    gfx_blend_scaled(buf, gfx_rect(90, 110, 3, 3), checker, gfx_rect(0, 0, 4, 4), 255);
    gfx_blend(buf, 100, 105, checker, gfx_rect(0, 0, 4, 4), 128);
    gfx_buffer_free(checker);

    golden("scene", buf);
    gfx_buffer_free(buf);
}

int main(void)
{
    test_arithmetic();
    test_rects();
    test_clipping();
    test_copy_overlap();
    test_present();
    test_scene();
    return hosttest_done("gfx");
}
