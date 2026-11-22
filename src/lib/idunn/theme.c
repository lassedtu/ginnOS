/**
 * @file theme.c
 * @brief idunn: the design tokens and the theme file.
 *
 * the values are the agreed look (plans/idunn-ui-plan.md, section 2, and
 * the "refined" preview). a theme is made in four steps: the table of its
 * mode, the accent, the tokens made from the accent, and last the tokens
 * the theme file changes for the mode.
 */

#include "idunn_theme.h"

#include <string.h>

static const char *const names[IDUNN_COLOR_COUNT] = {
    [IDUNN_COLOR_BACKGROUND] = "background",
    [IDUNN_COLOR_FOREGROUND] = "foreground",
    [IDUNN_COLOR_SURFACE] = "surface",
    [IDUNN_COLOR_SIDEBAR] = "sidebar",
    [IDUNN_COLOR_CARD] = "card",
    [IDUNN_COLOR_CARD_FOREGROUND] = "card-foreground",
    [IDUNN_COLOR_MUTED] = "muted",
    [IDUNN_COLOR_MUTED_FOREGROUND] = "muted-foreground",
    [IDUNN_COLOR_BORDER] = "border",
    [IDUNN_COLOR_INPUT] = "input",
    [IDUNN_COLOR_PRIMARY] = "primary",
    [IDUNN_COLOR_PRIMARY_FOREGROUND] = "primary-foreground",
    [IDUNN_COLOR_SECONDARY] = "secondary",
    [IDUNN_COLOR_SECONDARY_FOREGROUND] = "secondary-foreground",
    [IDUNN_COLOR_HOVER] = "hover",
    [IDUNN_COLOR_ACCENT] = "accent",
    [IDUNN_COLOR_ACCENT_FOREGROUND] = "accent-foreground",
    [IDUNN_COLOR_SELECTION] = "selection",
    [IDUNN_COLOR_DESTRUCTIVE] = "destructive",
    [IDUNN_COLOR_RING] = "ring",
    [IDUNN_COLOR_OVERLAY] = "overlay",
    [IDUNN_COLOR_TITLE_TOP] = "title-top",
    [IDUNN_COLOR_TITLE_BOTTOM] = "title-bottom",
    [IDUNN_COLOR_TOP_LIGHT] = "top-light",
};

// 0xAARRGGBB, not premultiplied; the accent and the tokens made from it
// are set later
static const uint32_t light[IDUNN_COLOR_COUNT] = {
    [IDUNN_COLOR_BACKGROUND] = 0xFFFFFFFF,
    [IDUNN_COLOR_FOREGROUND] = 0xFF09090B,
    [IDUNN_COLOR_SURFACE] = 0xFFFBFBFB,
    [IDUNN_COLOR_SIDEBAR] = 0xFFF6F6F7,
    [IDUNN_COLOR_CARD] = 0xFFFFFFFF,
    [IDUNN_COLOR_CARD_FOREGROUND] = 0xFF09090B,
    [IDUNN_COLOR_MUTED] = 0xFFF4F4F5,
    [IDUNN_COLOR_MUTED_FOREGROUND] = 0xFF71717A,
    [IDUNN_COLOR_BORDER] = 0xFFE4E4E7,
    [IDUNN_COLOR_INPUT] = 0xFFE4E4E7,
    [IDUNN_COLOR_SECONDARY] = 0xFFF4F4F5,
    [IDUNN_COLOR_SECONDARY_FOREGROUND] = 0xFF18181B,
    [IDUNN_COLOR_HOVER] = 0xFFF4F4F5,
    [IDUNN_COLOR_DESTRUCTIVE] = 0xFFDC2626,
    [IDUNN_COLOR_OVERLAY] = 0x66000000,
    [IDUNN_COLOR_TITLE_TOP] = 0xFFFDFDFD,
    [IDUNN_COLOR_TITLE_BOTTOM] = 0xFFF7F7F8,
    [IDUNN_COLOR_TOP_LIGHT] = 0xE6FFFFFF,
};

static const uint32_t dark[IDUNN_COLOR_COUNT] = {
    [IDUNN_COLOR_BACKGROUND] = 0xFF09090B,
    [IDUNN_COLOR_FOREGROUND] = 0xFFFAFAFA,
    [IDUNN_COLOR_SURFACE] = 0xFF0E0E10,
    [IDUNN_COLOR_SIDEBAR] = 0xFF0B0B0D,
    [IDUNN_COLOR_CARD] = 0xFF18181B,
    [IDUNN_COLOR_CARD_FOREGROUND] = 0xFFFAFAFA,
    [IDUNN_COLOR_MUTED] = 0xFF27272A,
    [IDUNN_COLOR_MUTED_FOREGROUND] = 0xFFA1A1AA,
    [IDUNN_COLOR_BORDER] = 0x1AFFFFFF,
    [IDUNN_COLOR_INPUT] = 0x26FFFFFF,
    [IDUNN_COLOR_SECONDARY] = 0xFF27272A,
    [IDUNN_COLOR_SECONDARY_FOREGROUND] = 0xFFFAFAFA,
    [IDUNN_COLOR_HOVER] = 0xFF27272A,
    [IDUNN_COLOR_DESTRUCTIVE] = 0xFFEF4444,
    [IDUNN_COLOR_OVERLAY] = 0x99000000,
    [IDUNN_COLOR_TITLE_TOP] = 0xFF18181B,
    [IDUNN_COLOR_TITLE_BOTTOM] = 0xFF131315,
    [IDUNN_COLOR_TOP_LIGHT] = 0x12FFFFFF,
};

typedef struct
{
    const char *name;
    uint32_t light;
    uint32_t dark;
    uint32_t light_text; // the text on it
    uint32_t dark_text;
} accent_t;

// Tailwind's 600 (light) and 500 (dark); neutral is shadcn's own primary
static const accent_t accents[] = {
    {"rose", 0xFFE11D48, 0xFFF43F5E, 0xFFFFFFFF, 0xFFFFFFFF},
    {"blue", 0xFF2563EB, 0xFF3B82F6, 0xFFFFFFFF, 0xFFFFFFFF},
    {"green", 0xFF16A34A, 0xFF22C55E, 0xFFFFFFFF, 0xFFFFFFFF},
    {"orange", 0xFFEA580C, 0xFFF97316, 0xFFFFFFFF, 0xFFFFFFFF},
    {"violet", 0xFF7C3AED, 0xFF8B5CF6, 0xFFFFFFFF, 0xFFFFFFFF},
    {"neutral", 0xFF18181B, 0xFFE4E4E7, 0xFFFAFAFA, 0xFF18181B},
};

typedef struct
{
    int32_t x;
    int32_t y;
    int32_t blur;
    int32_t spread;
    uint32_t color; // 0xAARRGGBB, not premultiplied
} layer_t;

typedef struct
{
    int count;
    layer_t layers[IDUNN_SHADOW_LAYERS]; // as in CSS: the first is on top
} shadow_spec_t;

static const shadow_spec_t light_shadows[IDUNN_SHADOW_COUNT] = {
    [IDUNN_SHADOW_XS] = {1, {{0, 1, 2, 0, 0x0D000000}}},
    [IDUNN_SHADOW_MD] = {3,
                         {{0, 0, 0, 1, 0x0A000000},
                          {0, 2, 4, 0, 0x0A000000},
                          {0, 8, 20, -4, 0x24000000}}},
    [IDUNN_SHADOW_WINDOW] = {4,
                             {{0, 0, 0, 1, 0x12000000},
                              {0, 1, 2, 0, 0x0D000000},
                              {0, 8, 18, -6, 0x2E000000},
                              {0, 28, 56, -16, 0x52000000}}},
};

static const shadow_spec_t dark_shadows[IDUNN_SHADOW_COUNT] = {
    [IDUNN_SHADOW_XS] = {1, {{0, 1, 2, 0, 0x4D000000}}},
    [IDUNN_SHADOW_MD] = {2, {{0, 0, 0, 1, 0x0FFFFFFF}, {0, 8, 24, -4, 0x99000000}}},
    [IDUNN_SHADOW_WINDOW] = {4,
                             {{0, 0, 0, 1, 0x17FFFFFF},
                              {0, 1, 2, 0, 0x66000000},
                              {0, 10, 22, -6, 0x80000000},
                              {0, 32, 64, -16, 0xA6000000}}},
};

static gfx_color_t premultiplied(uint32_t argb)
{
    return gfx_rgba((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                    (uint8_t)(argb >> 24));
}

/**
 * a colour with its alpha at percent of what it was.
 */
static uint32_t at_percent(uint32_t argb, uint32_t percent)
{
    uint32_t alpha = (argb >> 24) * percent / 100;
    return alpha << 24 | (argb & 0x00FFFFFF);
}

static long clamp(long value, long low, long high)
{
    return value < low ? low : value > high ? high : value;
}

/**
 * fill the colours of a mode with an accent (light and dark values and
 * the text on them), then the theme file's changes for the mode.
 */
static void make_mode(idunn_theme_t *theme, idunn_mode_t mode, const accent_t *accent,
                      const conf_t *conf)
{
    bool is_dark = mode == IDUNN_DARK;
    uint32_t argb[IDUNN_COLOR_COUNT];
    memcpy(argb, is_dark ? dark : light, sizeof(argb));

    argb[IDUNN_COLOR_ACCENT] = is_dark ? accent->dark : accent->light;
    argb[IDUNN_COLOR_ACCENT_FOREGROUND] = is_dark ? accent->dark_text : accent->light_text;
    argb[IDUNN_COLOR_PRIMARY] = argb[IDUNN_COLOR_ACCENT];
    argb[IDUNN_COLOR_PRIMARY_FOREGROUND] = argb[IDUNN_COLOR_ACCENT_FOREGROUND];
    argb[IDUNN_COLOR_SELECTION] = at_percent(argb[IDUNN_COLOR_ACCENT], is_dark ? 30 : 20);
    argb[IDUNN_COLOR_RING] = at_percent(argb[IDUNN_COLOR_ACCENT], 50);

    if (conf)
    {
        const char *section = is_dark ? "dark" : "light";
        for (int i = 0; i < IDUNN_COLOR_COUNT; i++)
            argb[i] = conf_get_color(conf, section, names[i], argb[i]);
    }
    for (int i = 0; i < IDUNN_COLOR_COUNT; i++)
        theme->colors[mode][i] = premultiplied(argb[i]);
}

static void make_colors(idunn_theme_t *theme, const accent_t *accent, const conf_t *conf)
{
    make_mode(theme, IDUNN_LIGHT, accent, conf);
    make_mode(theme, IDUNN_DARK, accent, conf);
}

void idunn_theme_default(idunn_theme_t *theme)
{
    theme->mode = IDUNN_DARK;
    theme->radius = 6;
    theme->font_size = 13;
    theme->scale = 1;
    theme->motion = true;
    make_colors(theme, &accents[0], NULL);
}

void idunn_theme_from_conf(idunn_theme_t *theme, const conf_t *conf)
{
    idunn_theme_default(theme);
    if (!conf)
        return;

    const char *mode = conf_get(conf, "theme", "mode", "dark");
    theme->mode = strcmp(mode, "light") == 0 ? IDUNN_LIGHT : IDUNN_DARK;
    theme->radius = (int32_t)clamp(conf_get_int(conf, "theme", "radius", 6), 4, 12);
    theme->font_size = (int32_t)clamp(conf_get_int(conf, "theme", "font_size", 13), 13, 15);
    theme->scale = (int32_t)clamp(conf_get_int(conf, "theme", "scale", 1), 1, 2);
    theme->motion = conf_get_bool(conf, "theme", "motion", true);

    // a named accent, or any colour (with white text, in both modes)
    accent_t accent = accents[0];
    const char *name = conf_get(conf, "theme", "accent", "rose");
    if (name[0] == '#')
    {
        uint32_t color = conf_get_color(conf, "theme", "accent", accents[0].light);
        accent = (accent_t){name, color, color, 0xFFFFFFFF, 0xFFFFFFFF};
    }
    else
    {
        for (size_t i = 0; i < sizeof(accents) / sizeof(accents[0]); i++)
        {
            if (strcmp(name, accents[i].name) == 0)
                accent = accents[i];
        }
    }
    make_colors(theme, &accent, conf);
}

void idunn_theme_load(idunn_theme_t *theme)
{
    conf_t *conf = conf_open("theme");
    idunn_theme_from_conf(theme, conf);
    conf_free(conf);
}

const char *idunn_color_name(idunn_color_t which)
{
    return which >= 0 && which < IDUNN_COLOR_COUNT ? names[which] : NULL;
}

int32_t idunn_radius(const idunn_theme_t *theme)
{
    return idunn_px(theme, theme->radius);
}

int32_t idunn_radius_sm(const idunn_theme_t *theme)
{
    return idunn_px(theme, theme->radius / 2);
}

int32_t idunn_radius_lg(const idunn_theme_t *theme)
{
    return idunn_px(theme, theme->radius + 4);
}

idunn_text_style_t idunn_text_style(const idunn_theme_t *theme, idunn_text_t which)
{
    // at the default 13 px: 13/18, 13/18, 12/16, 15/22, 19/26, 23/30, 12/18
    int32_t fs = theme->font_size;
    int32_t small = fs - 1 > 12 ? fs - 1 : 12;
    idunn_text_style_t style;
    switch (which)
    {
    case IDUNN_TEXT_LABEL:
        style = (idunn_text_style_t){fs, fs + 5, 500, -0.006f, false};
        break;
    case IDUNN_TEXT_SMALL:
        style = (idunn_text_style_t){small, small + 4, 400, 0.0f, false};
        break;
    case IDUNN_TEXT_TITLE:
        style = (idunn_text_style_t){fs + 2, fs + 9, 600, -0.01f, false};
        break;
    case IDUNN_TEXT_HEADING:
        style = (idunn_text_style_t){fs + 6, fs + 13, 600, -0.02f, false};
        break;
    case IDUNN_TEXT_DISPLAY:
        style = (idunn_text_style_t){fs + 10, fs + 17, 600, -0.02f, false};
        break;
    case IDUNN_TEXT_MONO:
        style = (idunn_text_style_t){fs - 1, fs + 5, 400, 0.0f, true};
        break;
    default:
        style = (idunn_text_style_t){fs, fs + 5, 400, -0.006f, false};
        break;
    }
    style.size = idunn_px(theme, style.size);
    style.line_height = idunn_px(theme, style.line_height);
    return style;
}

int idunn_shadow(const idunn_theme_t *theme, idunn_shadow_t which,
                 gfx_shadow_t layers[IDUNN_SHADOW_LAYERS])
{
    if (which < 0 || which >= IDUNN_SHADOW_COUNT)
        return 0;
    const shadow_spec_t *spec =
        theme->mode == IDUNN_DARK ? &dark_shadows[which] : &light_shadows[which];
    for (int i = 0; i < spec->count; i++)
    {
        // the last CSS layer is the bottom one, so it is drawn first
        const layer_t *l = &spec->layers[spec->count - 1 - i];
        layers[i] = (gfx_shadow_t){idunn_px(theme, l->x), idunn_px(theme, l->y),
                                   idunn_px(theme, l->blur), idunn_px(theme, l->spread),
                                   premultiplied(l->color), false};
    }
    return spec->count;
}
