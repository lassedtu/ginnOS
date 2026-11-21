/**
 * @file image_test.c
 * @brief host tests of the image library (make host-test).
 *
 * the test images are in data/ (make_data.py makes them).
 */

#include "image.h"
#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA TEST_DIR "/data/"

static void test_png(void)
{
    const char *error = NULL;
    gfx_buffer_t *img = image_load(DATA "alpha.png", &error);
    CHECK(img != NULL);
    if (!img)
        return;
    CHECK_EQ(img->width, 8);
    CHECK_EQ(img->height, 8);
    int wrong = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (*gfx_pixel_at(img, x, y) != gfx_rgba(x * 32, y * 32, 128, (x + y) * 16))
                wrong++;
    CHECK_EQ(wrong, 0);
    gfx_buffer_free(img);

    // grey becomes opaque RGB
    img = image_load(DATA "gray.png", &error);
    CHECK(img != NULL);
    if (img)
    {
        CHECK_EQ(*gfx_pixel_at(img, 3, 3), gfx_rgb(240, 240, 240));
        CHECK_EQ(*gfx_pixel_at(img, 1, 0), gfx_rgb(16, 16, 16));
        gfx_buffer_free(img);
    }
}

static void test_jpeg(void)
{
    const char *error = NULL;
    gfx_buffer_t *img = image_load(DATA "photo.jpg", &error);
    CHECK(img != NULL);
    if (!img)
        return;
    CHECK_EQ(img->width, 64);
    CHECK_EQ(img->height, 48);
    // lossy: close to the source colours, and opaque
    int worst = 0;
    for (int y = 0; y < 48; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            uint32_t p = *gfx_pixel_at(img, x, y);
            int want[3] = {x * 4, y * 5, 255 - x * 2};
            int got[3] = {(int)(p >> 16) & 255, (int)(p >> 8) & 255, (int)p & 255};
            for (int c = 0; c < 3; c++)
            {
                int d = abs(want[c] - got[c]);
                worst = d > worst ? d : worst;
            }
            CHECK(gfx_alpha(p) == 255 || !"opaque");
        }
    }
    CHECK(worst <= 12);

    // the decoder makes the same pixels every time (golden image)
    uint8_t *rgb = malloc(64 * 48 * 3);
    for (int i = 0; i < 64 * 48; i++)
    {
        rgb[i * 3] = (uint8_t)(img->pixels[i] >> 16);
        rgb[i * 3 + 1] = (uint8_t)(img->pixels[i] >> 8);
        rgb[i * 3 + 2] = (uint8_t)img->pixels[i];
    }
    hosttest_golden("photo", rgb, 64, 48);
    free(rgb);
    gfx_buffer_free(img);
}

static void test_errors(void)
{
    const char *error = NULL;
    CHECK(image_load(DATA "does-not-exist.png", &error) == NULL);
    CHECK(error != NULL);

    static const uint8_t garbage[] = "this is not an image at all";
    error = NULL;
    CHECK(image_decode(garbage, sizeof(garbage), &error) == NULL);
    CHECK(error != NULL);
    CHECK(image_decode(garbage, 0, NULL) == NULL);

    // the first half of a valid PNG
    FILE *f = fopen(DATA "alpha.png", "rb");
    uint8_t data[512];
    size_t size = f ? fread(data, 1, sizeof(data), f) : 0;
    if (f)
        fclose(f);
    CHECK(size > 50);
    CHECK(image_decode(data, size / 2, NULL) == NULL);
}

int main(void)
{
    test_png();
    test_jpeg();
    test_errors();
    return hosttest_done("image");
}
