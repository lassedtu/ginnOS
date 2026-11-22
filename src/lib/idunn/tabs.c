/**
 * @file tabs.c
 * @brief idunn: tabs: a muted strip of tab names, the active one a raised
 *        pill, and below it the page of the active tab (the others are
 *        hidden). the strip is one stop for Tab; the arrows go to the tab
 *        before or after it, as shadcn's tabs do.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define PAD      3  // inside the strip
#define GAP      2  // between tabs
#define TAB_H    24
#define TAB_PAD  12
#define PAGE_GAP 16 // between the strip and the page

typedef struct
{
    char **names;
    int count;
    int capacity;
    int active;
    int hover;  // the tab under the pointer, or -1
    idunn_callback_t on_change;
    void *data;
} tabs_t;

static int32_t tab_width(idunn_widget_t *w, int i)
{
    const tabs_t *s = w->data;
    bragi_face_t *face = idunn_face(idunn_ctx(w), IDUNN_TEXT_CAPTION);
    int32_t length = (int32_t)strlen(s->names[i]);
    return 2 * idunn_px(idunn_theme(w), TAB_PAD) +
           (face ? bragi_caret_x(face, s->names[i], length, length) : 0);
}

static int32_t strip_height(const idunn_widget_t *w)
{
    return idunn_px(idunn_theme(w), TAB_H + 2 * PAD);
}

/** the rect of tab i (the strip: i == -1) */
static gfx_rect_t tab_rect(idunn_widget_t *w, int i)
{
    const tabs_t *s = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad = idunn_px(t, PAD);
    int32_t x = w->rect.x + pad;
    for (int k = 0; k < s->count; k++)
    {
        int32_t width = tab_width(w, k);
        if (k == i)
            return gfx_rect(x, w->rect.y + pad, width, idunn_px(t, TAB_H));
        x += width + idunn_px(t, GAP);
    }
    // the whole strip
    int32_t end = s->count ? x - idunn_px(t, GAP) + pad : x + pad;
    return gfx_rect(w->rect.x, w->rect.y, end - w->rect.x, strip_height(w));
}

static void tabs_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    const tabs_t *s = w->data;
    *width = tab_rect(w, -1).w;
    *height = strip_height(w);
    if (s->active >= 0 && s->active < w->child_count)
    {
        int32_t pw;
        int32_t ph;
        idunn_measure(w->children[s->active], max_width, &pw, &ph);
        *width = pw > *width ? pw : *width;
        *height += idunn_px(idunn_theme(w), PAGE_GAP) + ph;
    }
}

static void tabs_layout(idunn_widget_t *w)
{
    const tabs_t *s = w->data;
    int32_t top = strip_height(w) + idunn_px(idunn_theme(w), PAGE_GAP);
    if (s->active >= 0 && s->active < w->child_count)
        idunn_place(w->children[s->active],
                    gfx_rect(w->rect.x, w->rect.y + top, w->rect.w, w->rect.h - top));
}

static void tabs_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const tabs_t *s = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    int32_t radius = idunn_radius(t);
    int32_t inner = radius - idunn_px(t, 2) > 0 ? radius - idunn_px(t, 2) : 0;
    gfx_fill_round_rect(buf, tab_rect(w, -1), radius, idunn_color(t, IDUNN_COLOR_MUTED));
    int32_t line = idunn_text_style(t, IDUNN_TEXT_CAPTION).line_height;
    for (int i = 0; i < s->count; i++)
    {
        gfx_rect_t r = tab_rect(w, i);
        bool active = i == s->active;
        if (active)
        {
            if (idunn_ring_visible(w))
                idunn_draw_ring(w, buf, r, inner);
            idunn_draw_shadow(t, buf, r, inner, IDUNN_SHADOW_XS);
            gfx_fill_round_rect(buf, r, inner,
                                t->mode == IDUNN_DARK ? gfx_rgb(0x3f, 0x3f, 0x46)
                                                      : idunn_color(t, IDUNN_COLOR_BACKGROUND));
        }
        bool bright = active || (i == s->hover && (w->state & IDUNN_HOVER));
        idunn_draw_text(ctx, buf, IDUNN_TEXT_CAPTION, r.x + idunn_px(t, TAB_PAD),
                        r.y + (r.h - line) / 2, 0, s->names[i],
                        idunn_color(t, bright ? IDUNN_COLOR_FOREGROUND
                                              : IDUNN_COLOR_MUTED_FOREGROUND));
    }
}

static void show(idunn_widget_t *w, int active, bool tell)
{
    tabs_t *s = w->data;
    if (active < 0 || active >= s->count || active == s->active)
        return;
    s->active = active;
    for (int i = 0; i < w->child_count; i++)
        idunn_set_hidden(w->children[i], i != active);
    idunn_relayout(w);
    idunn_dirty(w);
    if (tell && s->on_change)
        s->on_change(w, s->data);
}

static int tab_at(idunn_widget_t *w, int32_t x, int32_t y)
{
    const tabs_t *s = w->data;
    for (int i = 0; i < s->count; i++)
    {
        if (gfx_rect_contains(tab_rect(w, i), x, y))
            return i;
    }
    return -1;
}

static bool tabs_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    tabs_t *s = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_MOTION:
    {
        int hover = tab_at(w, ev->x, ev->y);
        if (hover != s->hover)
        {
            s->hover = hover;
            idunn_dirty(w);
        }
        return true;
    }
    case IDUNN_EV_PRESS:
    {
        int i = tab_at(w, ev->x, ev->y);
        if (i < 0)
            return false;
        show(w, i, true);
        return true;
    }
    case IDUNN_EV_KEY:
        if (ev->code == IDUNN_KEY_LEFT)
            show(w, s->active > 0 ? s->active - 1 : s->count - 1, true);
        else if (ev->code == IDUNN_KEY_RIGHT)
            show(w, s->active + 1 < s->count ? s->active + 1 : 0, true);
        else if (ev->code == IDUNN_KEY_HOME)
            show(w, 0, true);
        else if (ev->code == IDUNN_KEY_END)
            show(w, s->count - 1, true);
        else
            return false;
        return true;
    default:
        return false;
    }
}

static void tabs_free(idunn_widget_t *w)
{
    tabs_t *s = w->data;
    for (int i = 0; i < s->count; i++)
        free(s->names[i]);
    free(s->names);
    free(s);
}

static const idunn_class_t tabs_class = {
    .name = "tabs",
    .measure = tabs_measure,
    .layout = tabs_layout,
    .draw = tabs_draw,
    .event = tabs_event,
    .free = tabs_free,
};

idunn_widget_t *idunn_tabs(idunn_callback_t on_change, void *data)
{
    tabs_t *s = calloc(1, sizeof(*s));
    idunn_widget_t *w =
        s ? idunn_widget_new(&tabs_class, s, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE) : NULL;
    if (!w)
    {
        free(s);
        return NULL;
    }
    s->hover = -1;
    s->on_change = on_change;
    s->data = data;
    return w;
}

static tabs_t *as_tabs(const idunn_widget_t *w)
{
    return w && w->cls == &tabs_class ? w->data : NULL;
}

idunn_widget_t *idunn_tabs_add(idunn_widget_t *tabs, const char *name, idunn_widget_t *page)
{
    tabs_t *s = as_tabs(tabs);
    if (!s || !page)
    {
        idunn_widget_free(page);
        return NULL;
    }
    if (s->count == s->capacity)
    {
        int capacity = s->capacity ? s->capacity * 2 : 4;
        char **grown = realloc(s->names, (size_t)capacity * sizeof(char *));
        if (!grown)
        {
            idunn_widget_free(page);
            return NULL;
        }
        s->names = grown;
        s->capacity = capacity;
    }
    s->names[s->count++] = strdup(name ? name : "");
    idunn_add(tabs, page);
    // the first tab is active; the pages of the others are hidden
    if (s->count == 1)
        s->active = 0;
    else
        idunn_set_hidden(page, true);
    idunn_relayout(tabs);
    return page;
}

int idunn_tabs_active(const idunn_widget_t *tabs)
{
    const tabs_t *s = as_tabs(tabs);
    return s ? s->active : -1;
}

void idunn_tabs_set_active(idunn_widget_t *tabs, int active)
{
    if (as_tabs(tabs))
        show(tabs, active, false);
}
