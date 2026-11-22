/**
 * @file button.c
 * @brief idunn: buttons in the variants of the preview (shadcn's, with the
 *        refined finish and a soft red delete button).
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define ICON_SIZE 16
#define ICON_GAP  8

typedef struct
{
    char *text; // NULL: an icon button
    char *icon; // a Lucide name, or NULL
    idunn_variant_t variant;
    idunn_size_t size;
    idunn_align_t align; // the content: centred, or at the start
    idunn_callback_t on_click;
    void *data;
} button_t;

/** the fill when normal, under the pointer and pressed, and the text colour */
typedef struct
{
    gfx_color_t normal;
    gfx_color_t hover;
    gfx_color_t pressed;
    gfx_color_t text;
} look_t;

/** percent as an amount for gfx_mix and gfx_fade */
static uint8_t pct(int percent)
{
    return (uint8_t)(percent * 255 / 100);
}

static look_t look_of(const idunn_theme_t *t, idunn_variant_t variant)
{
    gfx_color_t fg = idunn_color(t, IDUNN_COLOR_FOREGROUND);
    gfx_color_t bg = idunn_color(t, IDUNN_COLOR_BACKGROUND);
    gfx_color_t hover = idunn_color(t, IDUNN_COLOR_HOVER);
    gfx_color_t none = 0;
    switch (variant)
    {
    case IDUNN_PRIMARY:
    {
        // color-mix(primary 88 %, background) under the pointer, 80 % pressed
        gfx_color_t p = idunn_color(t, IDUNN_COLOR_PRIMARY);
        return (look_t){p, gfx_mix(p, bg, pct(12)), gfx_mix(p, bg, pct(20)),
                        idunn_color(t, IDUNN_COLOR_PRIMARY_FOREGROUND)};
    }
    case IDUNN_SECONDARY:
    {
        gfx_color_t s = idunn_color(t, IDUNN_COLOR_SECONDARY);
        return (look_t){s, gfx_mix(s, fg, pct(9)), gfx_mix(s, fg, pct(14)),
                        idunn_color(t, IDUNN_COLOR_SECONDARY_FOREGROUND)};
    }
    case IDUNN_OUTLINE:
        return (look_t){idunn_color(t, IDUNN_COLOR_CARD), hover, hover, fg};
    case IDUNN_DESTRUCTIVE:
    {
        // soft red: the red at 12 % over the card, 18 % under the pointer
        gfx_color_t red = idunn_color(t, IDUNN_COLOR_DESTRUCTIVE);
        gfx_color_t card = idunn_color(t, IDUNN_COLOR_CARD);
        return (look_t){gfx_mix(card, red, pct(12)), gfx_mix(card, red, pct(18)),
                        gfx_mix(card, red, pct(24)), red};
    }
    case IDUNN_GHOST:
        return (look_t){none, hover, hover, fg};
    case IDUNN_LINK:
    default:
        return (look_t){none, none, none, fg};
    }
}

static int32_t height_of(const button_t *b)
{
    return b->size == IDUNN_SIZE_SMALL ? 28 : b->size == IDUNN_SIZE_LARGE ? 36 : 32;
}

static int32_t padding_of(const button_t *b)
{
    if (b->variant == IDUNN_LINK)
        return 4;
    return b->size == IDUNN_SIZE_SMALL ? 12 : b->size == IDUNN_SIZE_LARGE ? 20 : 16;
}

/** the width of the icon and the text side by side */
static int32_t content_width(idunn_widget_t *w)
{
    const button_t *b = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t width = b->icon ? idunn_px(t, ICON_SIZE) : 0;
    if (b->text)
    {
        // to the nearest pixel, as browsers lay out text (not up)
        bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_LABEL);
        int32_t length = (int32_t)strlen(b->text);
        width += (b->icon ? idunn_px(t, ICON_GAP) : 0) +
                 (face ? bragi_caret_x(face, b->text, length, length) : 0);
    }
    return width;
}

static void button_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const button_t *b = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    *height = idunn_px(t, height_of(b));
    // the content, the padding and a 1 px border (transparent in most
    // variants), as the preview's border-box buttons
    *width = b->text ? content_width(w) + 2 * idunn_px(t, padding_of(b) + 1) : *height;
}

/** the layers of the refined finish under the fill (outer shadows) */
static void draw_under(idunn_widget_t *w, gfx_buffer_t *buf, int32_t radius)
{
    const button_t *b = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    bool dark = t->mode == IDUNN_DARK;
    gfx_color_t black = gfx_rgb(0, 0, 0);
    int32_t one = idunn_px(t, 1);
    if (b->variant == IDUNN_PRIMARY)
    {
        // a rim 15 % darker than the colour and a small drop shadow
        gfx_shadow_t drop = {0, one, 2 * one, 0, gfx_fade(black, pct(14)), false};
        gfx_shadow_t rim = {0, 0, 0, one,
                            gfx_mix(idunn_color(t, IDUNN_COLOR_PRIMARY), black, pct(15)), false};
        gfx_box_shadow(buf, w->rect, radius, &drop);
        gfx_box_shadow(buf, w->rect, radius, &rim);
    }
    else if (b->variant == IDUNN_OUTLINE)
    {
        gfx_shadow_t drop = {0, one, 2 * one, 0, gfx_fade(black, dark ? pct(30) : pct(5)), false};
        gfx_box_shadow(buf, w->rect, radius, &drop);
    }
}

/** the layers of the refined finish over the fill (lines inside) */
static void draw_over(idunn_widget_t *w, gfx_buffer_t *buf, int32_t radius)
{
    const button_t *b = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    bool dark = t->mode == IDUNN_DARK;
    gfx_color_t white = gfx_rgb(255, 255, 255);
    gfx_color_t black = gfx_rgb(0, 0, 0);
    int32_t one = idunn_px(t, 1);
    switch (b->variant)
    {
    case IDUNN_PRIMARY:
    {
        // a light line at the top inside
        gfx_shadow_t top = {0, one, 0, 0, gfx_fade(white, pct(14)), true};
        gfx_box_shadow(buf, w->rect, radius, &top);
        break;
    }
    case IDUNN_OUTLINE:
    {
        gfx_shadow_t line = dark ? (gfx_shadow_t){0, one, 0, 0, gfx_fade(white, pct(4)), true}
                                 : (gfx_shadow_t){0, -one, 0, 0, gfx_fade(black, pct(3)), true};
        gfx_box_shadow(buf, w->rect, radius, &line);
        gfx_color_t border = idunn_ring_visible(w) ? idunn_color(t, IDUNN_COLOR_ACCENT)
                                                   : idunn_color(t, IDUNN_COLOR_INPUT);
        gfx_border_round_rect(buf, w->rect, radius, one, border);
        break;
    }
    case IDUNN_DESTRUCTIVE:
        gfx_border_round_rect(buf, w->rect, radius, one,
                              gfx_fade(idunn_color(t, IDUNN_COLOR_DESTRUCTIVE), pct(25)));
        break;
    default:
        break;
    }
}

static void button_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const button_t *b = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    int32_t radius = idunn_radius(t);
    look_t look = look_of(t, b->variant);

    if (idunn_ring_visible(w))
        idunn_draw_ring(w, buf, w->rect, radius);
    draw_under(w, buf, radius);

    gfx_color_t fill = gfx_mix(look.normal, look.hover, idunn_hover_amount(w));
    fill = gfx_mix(fill, look.pressed, idunn_press_amount(w));
    if (b->variant == IDUNN_PRIMARY)
    {
        // a gradient from white at 8 % to nothing over the colour
        gfx_color_t top = gfx_over(fill, gfx_fade(gfx_rgb(255, 255, 255), pct(8)));
        gfx_fill_round_rect_gradient(buf, w->rect, radius, top, fill);
    }
    else if (gfx_alpha(fill))
    {
        gfx_fill_round_rect(buf, w->rect, radius, fill);
    }
    draw_over(w, buf, radius);

    // the icon and the text, in the middle
    int32_t pad = idunn_px(t, padding_of(b) + 1);
    int32_t width = content_width(w);
    int32_t room = w->rect.w - 2 * pad;
    bool centred = b->align != IDUNN_START && (width < room || !b->text);
    int32_t x = w->rect.x + (centred ? (w->rect.w - width) / 2 : pad);
    if (b->icon)
    {
        const icon_t *icon = idunn_icon(ctx, b->icon, ICON_SIZE);
        int32_t size = idunn_px(t, ICON_SIZE);
        if (icon)
            icon_draw_mask(buf, icon, x, w->rect.y + (w->rect.h - size) / 2, look.text);
        x += size + idunn_px(t, ICON_GAP);
        room -= size + idunn_px(t, ICON_GAP);
    }
    if (b->text)
    {
        int32_t line = idunn_text_style(t, IDUNN_TEXT_LABEL).line_height;
        int32_t y = w->rect.y + (w->rect.h - line) / 2;
        idunn_draw_text(ctx, buf, IDUNN_TEXT_LABEL, x, y, room, b->text, look.text);
        if (b->variant == IDUNN_LINK && (w->state & IDUNN_HOVER))
        {
            // underlined, 4 px under the baseline
            bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_LABEL);
            int32_t under = y + bragi_line_baseline(face, line) + idunn_px(t, 3);
            int32_t text_width = bragi_measure(face, b->text, -1);
            gfx_fill_rect(buf, gfx_rect(x, under, text_width < room ? text_width : room,
                                        idunn_px(t, 1)),
                          look.text);
        }
    }
}

static void click(idunn_widget_t *w)
{
    const button_t *b = w->data;
    if (b->on_click)
        b->on_click(w, b->data);
}

static bool button_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    if (ev->type == IDUNN_EV_CLICK)
    {
        click(w);
        return true;
    }
    if (ev->type == IDUNN_EV_KEY && (ev->code == IDUNN_KEY_ENTER || ev->code == IDUNN_KEY_SPACE))
    {
        click(w);
        return true;
    }
    return false;
}

static void button_free(idunn_widget_t *w)
{
    button_t *b = w->data;
    free(b->text);
    free(b->icon);
    free(b);
}

static const idunn_class_t button_class = {
    .name = "button",
    .measure = button_measure,
    .draw = button_draw,
    .event = button_event,
    .free = button_free,
};

idunn_widget_t *idunn_button(const char *text, idunn_variant_t variant, idunn_callback_t on_click,
                             void *data)
{
    button_t *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->text = text ? strdup(text) : NULL;
    b->variant = variant;
    b->align = IDUNN_CENTER;
    b->on_click = on_click;
    b->data = data;
    idunn_widget_t *w = idunn_widget_new(&button_class, b, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
    {
        free(b->text);
        free(b);
    }
    return w;
}

idunn_widget_t *idunn_icon_button(const char *icon, idunn_variant_t variant,
                                  idunn_callback_t on_click, void *data)
{
    idunn_widget_t *w = idunn_button(NULL, variant, on_click, data);
    idunn_button_set_icon(w, icon);
    return w;
}

static button_t *as_button(idunn_widget_t *w)
{
    return w && w->cls == &button_class ? w->data : NULL;
}

void idunn_button_set_icon(idunn_widget_t *button, const char *icon)
{
    button_t *b = as_button(button);
    if (!b)
        return;
    free(b->icon);
    b->icon = icon ? strdup(icon) : NULL;
    idunn_relayout(button);
    idunn_dirty(button);
}

void idunn_button_set_size(idunn_widget_t *button, idunn_size_t size)
{
    button_t *b = as_button(button);
    if (!b)
        return;
    b->size = size;
    idunn_relayout(button);
}

void idunn_button_set_text(idunn_widget_t *button, const char *text)
{
    button_t *b = as_button(button);
    char *copy = b && text ? strdup(text) : NULL;
    if (!b || (text && !copy))
        return;
    free(b->text);
    b->text = copy;
    idunn_relayout(button);
    idunn_dirty(button);
}

void idunn_button_set_variant(idunn_widget_t *button, idunn_variant_t variant)
{
    button_t *b = as_button(button);
    if (!b)
        return;
    b->variant = variant;
    idunn_relayout(button);
    idunn_dirty(button);
}

void idunn_button_set_align(idunn_widget_t *button, idunn_align_t align)
{
    button_t *b = as_button(button);
    if (!b)
        return;
    b->align = align;
    idunn_dirty(button);
}
