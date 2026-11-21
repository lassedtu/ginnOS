/**
 * @file icon_test.c
 * @brief host tests of the icon library (make host-test), with the real packs.
 */

#include "icon.h"
#include "hosttest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static icon_set_t *lucide;
static icon_set_t *tela;

static const int32_t sizes[] = {16, 20, 24, 32, 48, 64};

/**
 * every name in a list file is in the set, at every size and scale.
 */
static void check_list(icon_set_t *set, const char *list)
{
    FILE *f = fopen(list, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    char line[128];
    int names = 0;
    int bad = 0;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "#\r\n")] = '\0';
        char *name = line;
        while (*name == ' ')
            name++;
        size_t len = strlen(name);
        while (len > 0 && name[len - 1] == ' ')
            name[--len] = '\0';
        if (len == 0)
            continue;
        names++;
        if (!icon_set_has(set, name))
        {
            printf("  missing %s\n", name);
            bad++;
            continue;
        }
        for (int i = 0; i < 6; i++)
        {
            for (int32_t scale = 1; scale <= 2; scale++)
            {
                icon_t *icon = icon_load(set, name, sizes[i], scale);
                if (!icon || icon->width != sizes[i] * scale || icon->height != sizes[i] * scale)
                    bad++;
                icon_free(icon);
            }
        }
    }
    fclose(f);
    CHECK(names > 20);
    CHECK_EQ(bad, 0);
}

static void test_open(void)
{
    CHECK(icon_set_open(PACK_DIR "/missing.pack") == NULL);
    CHECK(icon_set_open(LIST_DIR "/lucide.list") == NULL); // not a pack
    CHECK(icon_set_is_mask(lucide));
    CHECK(!icon_set_is_mask(tela));
    CHECK(icon_load(tela, "folder", 0, 1) == NULL);
    CHECK(icon_load(tela, "folder", 600, 2) == NULL);
}

static void test_sizes(void)
{
    // no 40 px image: the 48 px one, scaled down
    icon_t *icon = icon_load(tela, "folder", 40, 1);
    CHECK(icon != NULL && icon->width == 40 && icon->height == 40 && !icon->is_mask);
    icon_free(icon);
    // bigger than the biggest (128 px): scaled up
    icon = icon_load(lucide, "x", 100, 2);
    CHECK(icon != NULL && icon->width == 200 && icon->is_mask);
    icon_free(icon);

    // 16 px at 2x has the thin lines of 16 px, unlike 32 px at 1x
    icon_t *thin = icon_load(lucide, "minus", 16, 2);
    icon_t *thick = icon_load(lucide, "minus", 32, 1);
    int32_t sum_thin = 0;
    int32_t sum_thick = 0;
    for (int32_t i = 0; i < 32 * 32; i++)
    {
        sum_thin += thin->mask[i];
        sum_thick += thick->mask[i];
    }
    CHECK(sum_thin < sum_thick);
    icon_free(thin);
    icon_free(thick);
}

static void test_fallback(void)
{
    // text-x-csrc is in the pack; text-x-foo is not, nor text-x, nor
    // text: so it is "unknown"
    icon_t *a = icon_load(tela, "text-x-foo", 32, 1);
    icon_t *b = icon_load(tela, "unknown", 32, 1);
    CHECK(a != NULL && b != NULL);
    if (a && b)
        CHECK(memcmp(a->image->pixels, b->image->pixels, 32 * 32 * 4) == 0);
    icon_free(a);
    icon_free(b);

    // a name with a longer form falls back to the shorter one
    a = icon_load(tela, "folder-music-classical", 32, 1);
    b = icon_load(tela, "folder-music", 32, 1);
    CHECK(a && b && memcmp(a->image->pixels, b->image->pixels, 32 * 32 * 4) == 0);
    icon_free(a);
    icon_free(b);

    // Lucide has no "unknown": no icon
    CHECK(icon_load(lucide, "no-such-icon", 24, 1) == NULL);
}

/**
 * a row of Lucide icons in colours and Tela icons, on light and dark.
 */
static void test_scene(void)
{
    gfx_buffer_t *buf = gfx_buffer_new(400, 150);
    gfx_fill_rect(buf, gfx_rect(0, 0, 400, 75), gfx_rgb(245, 245, 248));
    gfx_fill_rect(buf, gfx_rect(0, 75, 400, 75), gfx_rgb(30, 32, 40));

    static const char *const lines[] = {"x", "minus", "square", "search", "settings",
                                        "volume-2", "wifi", "power", "folder-plus", "trash"};
    for (int32_t i = 0; i < 10; i++)
    {
        icon_t *small = icon_load(lucide, lines[i], 16, 1);
        icon_t *big = icon_load(lucide, lines[i], 24, 1);
        icon_draw_mask(buf, small, 8 + i * 36, 8, gfx_rgb(40, 42, 50));
        icon_draw_mask(buf, big, 8 + i * 36, 34, gfx_rgb(60, 110, 220));
        icon_draw_mask(buf, big, 8 + i * 36, 83, gfx_rgb(230, 232, 240));
        icon_free(small);
        icon_free(big);
    }
    static const char *const apps[] = {"utilities-terminal", "folder", "text-x-csrc", "doom",
                                       "computer", "image-x-generic", "user-trash"};
    for (int32_t i = 0; i < 7; i++)
    {
        icon_t *icon = icon_load(tela, apps[i], 32, 1);
        icon_draw(buf, icon, 8 + i * 40, 112, 255);
        icon_free(icon);
    }
    icon_t *faded = icon_load(tela, "folder", 32, 1);
    icon_draw(buf, faded, 290, 112, 100);
    icon_draw_mask(buf, faded, 330, 112, gfx_rgb(240, 170, 60));
    icon_free(faded);

    uint8_t *rgb = malloc(400 * 150 * 3);
    for (int32_t i = 0; i < 400 * 150; i++)
    {
        rgb[i * 3] = (uint8_t)(buf->pixels[i] >> 16);
        rgb[i * 3 + 1] = (uint8_t)(buf->pixels[i] >> 8);
        rgb[i * 3 + 2] = (uint8_t)buf->pixels[i];
    }
    hosttest_golden("icons", rgb, 400, 150);
    free(rgb);
    gfx_buffer_free(buf);
}

int main(void)
{
    lucide = icon_set_open(PACK_DIR "/lucide.pack");
    tela = icon_set_open(PACK_DIR "/tela.pack");
    CHECK(lucide != NULL && tela != NULL);
    if (!lucide || !tela)
        return hosttest_done("icon");

    check_list(lucide, LIST_DIR "/lucide.list");
    check_list(tela, LIST_DIR "/tela.list");
    test_open();
    test_sizes();
    test_fallback();
    test_scene();
    icon_set_close(lucide);
    icon_set_close(tela);
    return hosttest_done("icon");
}
