#pragma once

/**
 * @file idunn_theme.h
 * @brief idunn: the design tokens (colours, sizes, text styles, shadows).
 *
 * the tokens are the only place colours and sizes are written: widgets
 * and heimdalld use token names, never numbers. the values follow the
 * agreed look (plans/idunn-ui-plan.md, section 2), shadcn's "zinc" theme
 * with the user's accent colour.
 *
 * the theme file (/etc/ginnos/theme.conf and ~/.config/ginnos/theme.conf)
 * has these keys in [theme]: mode (light or dark), accent (rose, blue,
 * green, orange, violet, neutral, or #rrggbb), font_size (13 to 15; all
 * text styles follow), scale (1 or 2), motion (true or false) and radius
 * (4 to 12; the small and large radii follow). the sections [light] and
 * [dark] can change any colour token for that mode, by its name:
 * "border = #ffffff1a".
 */

#include <conf.h>
#include <gfx.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    IDUNN_LIGHT,
    IDUNN_DARK,
} idunn_mode_t;

typedef enum
{
    IDUNN_BACKGROUND,           // the content of a plain window
    IDUNN_FOREGROUND,           // normal text
    IDUNN_SURFACE,              // the window content, a little off the background
    IDUNN_SIDEBAR,              // a side bar, one step more
    IDUNN_CARD,                 // cards, fields, menus, dialogs
    IDUNN_CARD_FOREGROUND,
    IDUNN_MUTED,                // quiet backgrounds: tab lists, code, empty states
    IDUNN_MUTED_FOREGROUND,     // help text, placeholders, inactive things
    IDUNN_BORDER,
    IDUNN_INPUT,                // the border of inputs
    IDUNN_PRIMARY,              // the main button: the accent
    IDUNN_PRIMARY_FOREGROUND,
    IDUNN_SECONDARY,
    IDUNN_SECONDARY_FOREGROUND,
    IDUNN_HOVER,                // ghost buttons, menu items and rows under the pointer
    IDUNN_ACCENT,               // the user's colour
    IDUNN_ACCENT_FOREGROUND,
    IDUNN_SELECTION,            // selected text and rows: the accent, faint
    IDUNN_DESTRUCTIVE,          // delete buttons, errors
    IDUNN_RING,                 // the focus ring: the accent at half
    IDUNN_OVERLAY,              // behind a dialog
    IDUNN_TITLE_TOP,            // the title bar's gradient
    IDUNN_TITLE_BOTTOM,
    IDUNN_TOP_LIGHT,            // the light line at the top edge of a title bar
    IDUNN_COLOR_COUNT
} idunn_color_t;

typedef enum
{
    IDUNN_TEXT_BODY,    // almost all text, inputs, menu items
    IDUNN_TEXT_LABEL,   // buttons, labels of fields, tabs
    IDUNN_TEXT_SMALL,   // help text, captions, badges
    IDUNN_TEXT_TITLE,   // card and dialog titles
    IDUNN_TEXT_HEADING, // page titles
    IDUNN_TEXT_DISPLAY, // the first page of an app
    IDUNN_TEXT_MONO,    // code, paths
    IDUNN_TEXT_COUNT
} idunn_text_t;

typedef struct
{
    int32_t size;        // pixels
    int32_t line_height; // pixels
    int32_t weight;      // 400, 500 or 600 (Inter Regular, Medium, SemiBold)
    float tracking;      // letter spacing in ems (bragi_face_set_tracking)
    bool mono;           // JetBrains Mono instead of Inter
} idunn_text_style_t;

typedef enum
{
    IDUNN_SHADOW_XS,     // inputs, buttons, the selected side bar item
    IDUNN_SHADOW_MD,     // menus, popovers, tooltips
    IDUNN_SHADOW_WINDOW, // windows and dialogs
    IDUNN_SHADOW_COUNT
} idunn_shadow_t;

#define IDUNN_SHADOW_LAYERS 4

typedef struct
{
    idunn_mode_t mode;
    gfx_color_t colors[IDUNN_COLOR_COUNT];
    int32_t radius;    // the base radius at scale 1
    int32_t font_size; // the body text size at scale 1
    int32_t scale;
    bool motion;
} idunn_theme_t;

/**
 * the default theme: dark, rose, 13 px text, 6 px radius.
 */
void idunn_theme_default(idunn_theme_t *theme);

/**
 * a theme from a theme file (or from text, for tests); missing or bad
 * values keep their defaults.
 */
void idunn_theme_from_conf(idunn_theme_t *theme, const conf_t *conf);

/**
 * read the user's theme (conf_open("theme")).
 */
void idunn_theme_load(idunn_theme_t *theme);

static inline gfx_color_t idunn_color(const idunn_theme_t *theme, idunn_color_t which)
{
    return theme->colors[which];
}

/**
 * the name of a colour token in the theme file ("muted-foreground"), or
 * NULL.
 */
const char *idunn_color_name(idunn_color_t which);

/**
 * a size of the design at the theme's scale: idunn_px(theme, 32).
 */
static inline int32_t idunn_px(const idunn_theme_t *theme, int32_t px)
{
    return px * theme->scale;
}

/** buttons, inputs, list rows */
int32_t idunn_radius(const idunn_theme_t *theme);
/** check boxes, small items in menus: half the radius */
int32_t idunn_radius_sm(const idunn_theme_t *theme);
/** cards, dialogs, popovers, windows: the radius + 4 */
int32_t idunn_radius_lg(const idunn_theme_t *theme);

/**
 * a text style at the theme's font size and scale.
 */
idunn_text_style_t idunn_text_style(const idunn_theme_t *theme, idunn_text_t which);

/**
 * the layers of a shadow, in the order to draw them (the CSS list from
 * its end), at the theme's scale.
 * @return the number of layers (at most IDUNN_SHADOW_LAYERS).
 */
int idunn_shadow(const idunn_theme_t *theme, idunn_shadow_t which,
                 gfx_shadow_t layers[IDUNN_SHADOW_LAYERS]);
