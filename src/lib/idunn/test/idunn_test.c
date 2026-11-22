/**
 * @file idunn_test.c
 * @brief host tests of idunn's theme (make host-test).
 */

#include "hosttest.h"
#include "idunn_theme.h"

#include <stdlib.h>

void test_widgets(void);
#include <string.h>

static void test_defaults(void)
{
    idunn_theme_t t;
    idunn_theme_default(&t);
    CHECK(t.mode == IDUNN_DARK);
    CHECK_EQ(t.font_size, 13);
    CHECK_EQ(idunn_radius(&t), 6);
    CHECK_EQ(idunn_radius_sm(&t), 3);
    CHECK_EQ(idunn_radius_lg(&t), 10);
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_BACKGROUND), gfx_rgb(0x09, 0x09, 0x0b));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT), gfx_rgb(0xf4, 0x3f, 0x5e));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_PRIMARY), idunn_color(&t, IDUNN_COLOR_ACCENT));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_RING), gfx_rgba(0xf4, 0x3f, 0x5e, 127));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_BORDER), gfx_rgba(255, 255, 255, 26));

    // every token has a name
    for (int i = 0; i < IDUNN_COLOR_COUNT; i++)
        CHECK(idunn_color_name(i) != NULL);
    CHECK(strcmp(idunn_color_name(IDUNN_COLOR_MUTED_FOREGROUND), "muted-foreground") == 0);
    CHECK(idunn_color_name(IDUNN_COLOR_COUNT) == NULL);
}

static void test_file(void)
{
    // the image's file, then the user's changes
    conf_t *conf = conf_from_text("[theme]\nmode = dark\naccent = rose\n",
                                  "[theme]\nmode = light\naccent = blue\nradius = 8\n"
                                  "font_size = 14\n[light]\nborder = #d4d4d8\n"
                                  "[dark]\nborder = #ff0000\n");
    idunn_theme_t t;
    idunn_theme_from_conf(&t, conf);
    CHECK(t.mode == IDUNN_LIGHT);
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT), gfx_rgb(0x25, 0x63, 0xeb));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_SELECTION), gfx_rgba(0x25, 0x63, 0xeb, 51));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_BORDER), gfx_rgb(0xd4, 0xd4, 0xd8));
    CHECK_EQ(idunn_radius_lg(&t), 12);
    CHECK_EQ(idunn_text_style(&t, IDUNN_TEXT_BODY).size, 14);
    conf_free(conf);

    // any colour as the accent; values out of range are made to fit
    conf = conf_from_text("[theme]\naccent = #123456\nradius = 40\nscale = 2\n", NULL);
    idunn_theme_from_conf(&t, conf);
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT), gfx_rgb(0x12, 0x34, 0x56));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT_FOREGROUND), gfx_rgb(255, 255, 255));
    CHECK_EQ(idunn_radius(&t), 24); // 12 at scale 2
    conf_free(conf);

    // an unknown accent and no file: the defaults
    conf = conf_from_text("[theme]\naccent = sparkly\n", NULL);
    idunn_theme_from_conf(&t, conf);
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT), gfx_rgb(0xf4, 0x3f, 0x5e));
    conf_free(conf);
    idunn_theme_from_conf(&t, NULL);
    CHECK(t.mode == IDUNN_DARK);

    // the neutral accent has dark text in dark mode
    conf = conf_from_text("[theme]\naccent = neutral\n", NULL);
    idunn_theme_from_conf(&t, conf);
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_PRIMARY_FOREGROUND), gfx_rgb(0x18, 0x18, 0x1b));
    conf_free(conf);
}

static void test_text_styles(void)
{
    idunn_theme_t t;
    idunn_theme_default(&t);
    static const int32_t sizes[IDUNN_TEXT_COUNT][2] = {
        {13, 18}, {13, 18}, {12, 16}, {15, 22}, {19, 26}, {23, 30}, {12, 18}, {12, 16}};
    for (int i = 0; i < IDUNN_TEXT_COUNT; i++)
    {
        idunn_text_style_t s = idunn_text_style(&t, i);
        CHECK_EQ(s.size, sizes[i][0]);
        CHECK_EQ(s.line_height, sizes[i][1]);
    }
    CHECK_EQ(idunn_text_style(&t, IDUNN_TEXT_LABEL).weight, 500);
    CHECK(idunn_text_style(&t, IDUNN_TEXT_MONO).mono);

    // small text stays at least 12 px
    t.font_size = 15;
    CHECK_EQ(idunn_text_style(&t, IDUNN_TEXT_SMALL).size, 14);
    t.font_size = 13;
    t.scale = 2;
    CHECK_EQ(idunn_text_style(&t, IDUNN_TEXT_SMALL).size, 24);
}

static void test_shadows(void)
{
    idunn_theme_t t;
    idunn_theme_default(&t);
    gfx_shadow_t layers[IDUNN_SHADOW_LAYERS];
    CHECK_EQ(idunn_shadow(&t, IDUNN_SHADOW_WINDOW, layers), 4);
    // drawn from the bottom: the wide shadow first, the 1 px edge last
    CHECK_EQ(layers[0].blur, 64);
    CHECK_EQ(layers[0].spread, -16);
    CHECK_EQ(layers[3].blur, 0);
    CHECK_EQ(layers[3].spread, 1);
    CHECK(!layers[3].inset);

    t.mode = IDUNN_LIGHT;
    CHECK_EQ(idunn_shadow(&t, IDUNN_SHADOW_XS, layers), 1);
    // both palettes are there: a mode change is all it takes
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_ACCENT), gfx_rgb(0xe1, 0x1d, 0x48));
    CHECK_EQ(idunn_color(&t, IDUNN_COLOR_BACKGROUND), gfx_rgb(255, 255, 255));
    CHECK_EQ(layers[0].y, 1);
    CHECK_EQ(idunn_shadow(&t, IDUNN_SHADOW_COUNT, layers), 0);
}

int main(void)
{
    test_defaults();
    test_file();
    test_text_styles();
    test_shadows();
    test_widgets();
    return hosttest_done("idunn");
}
