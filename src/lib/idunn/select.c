/**
 * @file select.c
 * @brief idunn: a select: a button like a text field with the chosen
 *        value and a chevron. a click, Space, Enter or the arrows open a
 *        menu of the choices under it, as wide as it, with a check at the
 *        chosen one.
 */

#include "idunn_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEIGHT 32
#define PAD    12
#define ICON   16
#define WIDTH  200

typedef struct
{
    char **choices;
    int count;
    int chosen;
    char *placeholder;
    idunn_menu_t *menu; // while it is open
    idunn_callback_t on_change;
    void *data;
} select_t;

static void select_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const idunn_theme_t *t = idunn_theme(w);
    *width = idunn_px(t, WIDTH);
    *height = idunn_px(t, HEIGHT);
}

static void select_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const select_t *s = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    idunn_draw_well(w, buf);
    int32_t size = idunn_px(t, ICON);
    int32_t pad = idunn_px(t, PAD + 1);
    const icon_t *chevron = idunn_icon(ctx, "chevron-down", ICON);
    gfx_color_t muted = idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND);
    if (chevron)
        icon_draw_mask(buf, chevron, w->rect.x + w->rect.w - pad - size,
                       w->rect.y + (w->rect.h - size) / 2, muted);
    bool chosen = s->chosen >= 0 && s->chosen < s->count;
    const char *text = chosen ? s->choices[s->chosen] : s->placeholder;
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    if (text)
        idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, w->rect.x + pad, w->rect.y + (w->rect.h - line) / 2,
                        w->rect.w - 2 * pad - size - idunn_px(t, 8), text,
                        chosen ? idunn_color(t, IDUNN_COLOR_FOREGROUND) : muted);
}

static void picked(idunn_widget_t *w, void *data)
{
    select_t *s = w->data;
    int i = (int)(intptr_t)data;
    if (i == s->chosen)
        return;
    s->chosen = i;
    idunn_dirty(w);
    if (s->on_change)
        s->on_change(w, s->data);
}

static void menu_closed(void *data)
{
    idunn_widget_t *w = data;
    select_t *s = w->data;
    idunn_menu_free(s->menu);
    s->menu = NULL;
}

static void open_menu(idunn_widget_t *w)
{
    select_t *s = w->data;
    if (s->menu || s->count == 0)
        return;
    s->menu = idunn_menu_new();
    if (!s->menu)
        return;
    for (int i = 0; i < s->count; i++)
        idunn_menu_add_check(s->menu, s->choices[i], i == s->chosen, picked, (void *)(intptr_t)i);
    // as wide as the select, 4 pixels under it
    const idunn_theme_t *t = idunn_theme(w);
    idunn_menu_set_min_width(s->menu, w->rect.w / (t->scale > 0 ? t->scale : 1));
    gfx_rect_t r = w->rect;
    r.h += idunn_px(t, 4);
    if (!idunn_menu_open(s->menu, w, r, s->chosen >= 0 ? s->chosen : 0, menu_closed, w))
    {
        idunn_menu_free(s->menu);
        s->menu = NULL;
    }
}

static bool select_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    if (ev->type == IDUNN_EV_CLICK)
    {
        open_menu(w);
        return true;
    }
    if (ev->type == IDUNN_EV_KEY &&
        (ev->code == IDUNN_KEY_SPACE || ev->code == IDUNN_KEY_DOWN || ev->code == IDUNN_KEY_UP))
    {
        open_menu(w);
        return true;
    }
    return false;
}

static void free_select(select_t *s)
{
    for (int i = 0; s->choices && i < s->count; i++)
        free(s->choices[i]);
    free(s->choices);
    free(s->placeholder);
    idunn_menu_free(s->menu);
    free(s);
}

static void select_free(idunn_widget_t *w)
{
    select_t *s = w->data;
    // an open menu goes first: it points at the choices
    if (s->menu && w->window && w->window->popup)
        idunn_popup_close(w->window->popup);
    free_select(s);
}

static const idunn_class_t select_class = {
    .name = "select",
    .measure = select_measure,
    .draw = select_draw,
    .event = select_event,
    .free = select_free,
};

idunn_widget_t *idunn_select(const char *const *choices, int count, int chosen,
                             idunn_callback_t on_change, void *data)
{
    select_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->choices = calloc((size_t)(count > 0 ? count : 1), sizeof(char *));
    for (int i = 0; s->choices && i < count; i++)
        s->choices[i] = strdup(choices[i] ? choices[i] : "");
    s->count = s->choices ? count : 0;
    s->chosen = chosen >= 0 && chosen < count ? chosen : -1;
    s->on_change = on_change;
    s->data = data;
    idunn_widget_t *w = idunn_widget_new(&select_class, s, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE);
    if (!w)
    {
        free_select(s);
        return NULL;
    }
    return w;
}

static select_t *as_select(const idunn_widget_t *w)
{
    return w && w->cls == &select_class ? w->data : NULL;
}

int idunn_select_chosen(const idunn_widget_t *select)
{
    const select_t *s = as_select(select);
    return s ? s->chosen : -1;
}

void idunn_select_choose(idunn_widget_t *select, int chosen)
{
    select_t *s = as_select(select);
    if (!s)
        return;
    s->chosen = chosen >= 0 && chosen < s->count ? chosen : -1;
    idunn_dirty(select);
}

void idunn_select_set_placeholder(idunn_widget_t *select, const char *text)
{
    select_t *s = as_select(select);
    if (!s)
        return;
    free(s->placeholder);
    s->placeholder = text ? strdup(text) : NULL;
    idunn_dirty(select);
}
