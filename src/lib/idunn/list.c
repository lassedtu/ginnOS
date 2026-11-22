/**
 * @file list.c
 * @brief idunn: a list view: rows of 32 pixels with an icon, text and a
 *        muted second text on the right; and a grid view, the same with
 *        tiles: a 48 pixel colour icon and the name below it (a file
 *        manager's icons). the item under the pointer has the hover colour,
 *        selected items the selection colour. only the items that can be
 *        seen are drawn, so a long list in a scroll view is fast.
 *
 * keys: the arrows, Page Up and Page Down, Home and End move the current
 * row (with Shift, they select from the anchor when many can be
 * selected); Space selects; Enter activates; Ctrl+A selects all; typing
 * finds the next row that starts with what was typed. the mouse: a press
 * selects, Ctrl toggles a row, Shift selects a range, a double click
 * activates.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define PAD      4
#define ROW      32
#define ROW_GAP  1
#define ROW_PAD  10
#define ICON     16
#define ICON_GAP 10
#define TILE_W   96   // a grid tile
#define TILE_H   88
#define PICTURE  48
#define TILE_GAP 4
#define FIND_MS  1000 // typed letters are one search for this long
#define FIND_MAX 32

typedef struct
{
    char *icon;
    char *text;
    char *detail;
    bool selected;
} item_t;

typedef struct
{
    item_t *items;
    int count;
    int capacity;
    int current; // the row with the keyboard, or -1
    int anchor;  // where a Shift range starts
    int hover;   // the row under the pointer, or -1
    bool multiple;
    bool framed;
    bool grid;
    char find[FIND_MAX];
    uint32_t find_at;
    idunn_callback_t on_select;
    void *select_data;
    idunn_callback_t on_activate;
    void *activate_data;
} list_t;

static int32_t pad_of(const idunn_widget_t *w)
{
    return idunn_px(idunn_theme(w), PAD);
}

/** the size of an item and the space to the next one */
static void cell(const idunn_widget_t *w, int32_t *width, int32_t *height, int32_t *gap)
{
    const list_t *l = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    *width = l->grid ? idunn_px(t, TILE_W) : w->rect.w - 2 * pad_of(w);
    *height = idunn_px(t, l->grid ? TILE_H : ROW);
    *gap = idunn_px(t, l->grid ? TILE_GAP : ROW_GAP);
}

/** items in a line: as many tiles as fit, or one row */
static int columns_for(const idunn_widget_t *w, int32_t width)
{
    const list_t *l = w->data;
    if (!l->grid)
        return 1;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t step = idunn_px(t, TILE_W + TILE_GAP);
    int n = (width - 2 * pad_of(w) + idunn_px(t, TILE_GAP)) / step;
    return n > 0 ? n : 1;
}

static int columns(const idunn_widget_t *w)
{
    return columns_for(w, w->rect.w);
}

/** the height of a line of items and the gap below it */
static int32_t pitch(const idunn_widget_t *w)
{
    int32_t cw;
    int32_t ch;
    int32_t gap;
    cell(w, &cw, &ch, &gap);
    return ch + gap;
}

static gfx_rect_t item_rect(const idunn_widget_t *w, int i)
{
    int32_t cw;
    int32_t ch;
    int32_t gap;
    cell(w, &cw, &ch, &gap);
    int cols = columns(w);
    int32_t pad = pad_of(w);
    return gfx_rect(w->rect.x + pad + (i % cols) * (cw + gap), w->rect.y + pad + (i / cols) * (ch + gap),
                    cw, ch);
}

static int item_at(const idunn_widget_t *w, int32_t x, int32_t y)
{
    const list_t *l = w->data;
    int32_t cw;
    int32_t ch;
    int32_t gap;
    cell(w, &cw, &ch, &gap);
    int32_t ax = x - w->rect.x - pad_of(w);
    int32_t ay = y - w->rect.y - pad_of(w);
    if (ax < 0 || ay < 0)
        return -1;
    int col = ax / (cw + gap);
    int line = ay / (ch + gap);
    int i = line * columns(w) + col;
    // the gaps belong to no item
    bool inside = ax - col * (cw + gap) < cw && ay - line * (ch + gap) < ch;
    return inside && col < columns(w) && i < l->count ? i : -1;
}

static void list_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    const list_t *l = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int cols = l->grid ? columns_for(w, max_width > 0 ? max_width : idunn_px(t, 4 * TILE_W)) : 1;
    int lines = (l->count + cols - 1) / cols;
    int32_t ch = idunn_px(t, l->grid ? TILE_H : ROW);
    int32_t gap = idunn_px(t, l->grid ? TILE_GAP : ROW_GAP);
    *width = l->grid ? 2 * pad_of(w) + cols * (idunn_px(t, TILE_W) + gap) - gap : idunn_px(t, 240);
    *height = 2 * pad_of(w) + (lines ? lines * (ch + gap) - gap : 0);
}

/** the selection, hover and ring behind an item */
static void draw_back(idunn_widget_t *w, gfx_buffer_t *buf, int i, gfx_rect_t r, int32_t radius)
{
    const list_t *l = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    if (l->items[i].selected)
        gfx_fill_round_rect(buf, r, radius, idunn_color(t, IDUNN_COLOR_SELECTION));
    else if (i == l->hover && (w->state & IDUNN_HOVER))
        gfx_fill_round_rect(buf, r, radius, idunn_color(t, IDUNN_COLOR_HOVER));
    if (i == l->current && idunn_ring_visible(w))
        idunn_draw_ring(w, buf, r, radius);
}

static void draw_tile(idunn_widget_t *w, gfx_buffer_t *buf, int i)
{
    const list_t *l = w->data;
    const item_t *it = &l->items[i];
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t r = item_rect(w, i);
    draw_back(w, buf, i, r, idunn_radius(t));
    int32_t size = idunn_px(t, PICTURE);
    int32_t top = r.y + idunn_px(t, 8);
    const icon_t *picture = idunn_picture(ctx, it->icon ? it->icon : "unknown", PICTURE);
    if (picture)
        icon_draw(buf, picture, r.x + (r.w - size) / 2, top, 255);
    // the name in the middle, with "..." when it is too long
    bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_BODY);
    int32_t room = r.w - idunn_px(t, 8);
    int32_t tw = face ? bragi_measure(face, it->text, -1) : 0;
    tw = tw < room ? tw : room;
    idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, r.x + (r.w - tw) / 2, top + size + idunn_px(t, 6),
                    room, it->text, idunn_color(t, IDUNN_COLOR_FOREGROUND));
}

static void draw_row(idunn_widget_t *w, gfx_buffer_t *buf, int i)
{
    const list_t *l = w->data;
    const item_t *it = &l->items[i];
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t r = item_rect(w, i);
    draw_back(w, buf, i, r, idunn_radius(t) - idunn_px(t, 2));

    int32_t x = r.x + idunn_px(t, ROW_PAD);
    int32_t right = r.x + r.w - idunn_px(t, ROW_PAD);
    if (it->icon)
    {
        int32_t size = idunn_px(t, ICON);
        const icon_t *icon = idunn_icon(ctx, it->icon, ICON);
        if (icon)
            icon_draw_mask(buf, icon, x, r.y + (r.h - size) / 2,
                           idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND));
        x += size + idunn_px(t, ICON_GAP);
    }
    if (it->detail)
    {
        bragi_face_t *small = idunn_face(ctx, IDUNN_TEXT_SMALL);
        int32_t dw = small ? bragi_measure(small, it->detail, -1) : 0;
        int32_t line = idunn_text_style(t, IDUNN_TEXT_SMALL).line_height;
        idunn_draw_text(ctx, buf, IDUNN_TEXT_SMALL, right - dw, r.y + (r.h - line) / 2, 0,
                        it->detail, idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND));
        right -= dw + idunn_px(t, ICON_GAP);
    }
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, r.y + (r.h - line) / 2, right - x, it->text,
                    idunn_color(t, IDUNN_COLOR_FOREGROUND));
}

static void list_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const list_t *l = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    if (l->framed)
    {
        int32_t radius = idunn_radius_lg(t);
        idunn_draw_shadow(t, buf, w->rect, radius, IDUNN_SHADOW_XS);
        gfx_fill_round_rect(buf, w->rect, radius, idunn_color(t, IDUNN_COLOR_CARD));
        gfx_border_round_rect(buf, w->rect, radius, idunn_px(t, 1),
                              idunn_color(t, IDUNN_COLOR_BORDER));
    }
    // only the rows in the clip (a scroll view shows a few of many)
    gfx_rect_t clip = gfx_rect_intersect(buf->clip, w->rect);
    if (gfx_rect_empty(clip) || l->count == 0)
        return;
    int32_t ring = idunn_overdraw(t);
    int cols = columns(w);
    int first = (clip.y - ring - w->rect.y - pad_of(w)) / pitch(w) * cols;
    int last = ((clip.y + clip.h + ring - w->rect.y - pad_of(w)) / pitch(w) + 1) * cols - 1;
    first = first < 0 ? 0 : first;
    last = last >= l->count ? l->count - 1 : last;
    for (int i = first; i <= last; i++)
    {
        if (l->grid)
            draw_tile(w, buf, i);
        else
            draw_row(w, buf, i);
    }
}

static void dirty_row(idunn_widget_t *w, int i)
{
    const list_t *l = w->data;
    if (i >= 0 && i < l->count)
        idunn_dirty_rect(w, item_rect(w, i));
}

static void selection_changed(idunn_widget_t *w)
{
    list_t *l = w->data;
    if (l->on_select)
        l->on_select(w, l->select_data);
}

static void select_only(idunn_widget_t *w, int i)
{
    list_t *l = w->data;
    for (int k = 0; k < l->count; k++)
    {
        bool want = k == i;
        if (l->items[k].selected != want)
        {
            l->items[k].selected = want;
            dirty_row(w, k);
        }
    }
}

static void select_range(idunn_widget_t *w, int from, int to)
{
    list_t *l = w->data;
    int lo = from < to ? from : to;
    int hi = from < to ? to : from;
    for (int k = 0; k < l->count; k++)
    {
        bool want = k >= lo && k <= hi;
        if (l->items[k].selected != want)
        {
            l->items[k].selected = want;
            dirty_row(w, k);
        }
    }
}

/** make row i the current one and scroll it into view */
static void set_current(idunn_widget_t *w, int i)
{
    list_t *l = w->data;
    if (i < 0 || i >= l->count)
        return;
    dirty_row(w, l->current);
    l->current = i;
    dirty_row(w, i);
    int32_t ring = idunn_overdraw(idunn_theme(w));
    gfx_rect_t r = item_rect(w, i);
    idunn_reveal(w, gfx_rect(r.x - ring, r.y - ring, r.w + 2 * ring, r.h + 2 * ring));
}

/** move the current row by the keyboard, selecting as it goes */
static void move_to(idunn_widget_t *w, int i, bool extend)
{
    list_t *l = w->data;
    if (l->count == 0)
        return;
    i = i < 0 ? 0 : i >= l->count ? l->count - 1 : i;
    set_current(w, i);
    if (extend && l->multiple)
    {
        select_range(w, l->anchor, i);
    }
    else
    {
        l->anchor = i;
        select_only(w, i);
    }
    selection_changed(w);
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool starts_with(const char *text, const char *prefix)
{
    for (; *prefix; text++, prefix++)
    {
        if (lower(*text) != lower(*prefix))
            return false;
    }
    return true;
}

/** type to find: the next row from the current one that starts with the typed text */
static void find(idunn_widget_t *w, const char *typed)
{
    list_t *l = w->data;
    uint32_t now = w->window->now;
    size_t have = strlen(l->find);
    if (now - l->find_at > FIND_MS)
        have = 0;
    l->find_at = now;
    size_t add = strlen(typed);
    if (have + add < FIND_MAX)
    {
        memcpy(l->find + have, typed, add);
        have += add;
    }
    l->find[have] = '\0';
    // one letter again and again goes on to the next row with it
    bool same = have > 1;
    for (size_t k = 1; k < have && same; k++)
        same = l->find[k] == l->find[0];
    const char *what = same ? typed : l->find;
    int from = l->current < 0 ? 0 : l->current + (have == 1 || same ? 1 : 0);
    for (int n = 0; n < l->count; n++)
    {
        int i = (from + n) % l->count;
        if (starts_with(l->items[i].text, what))
        {
            move_to(w, i, false);
            return;
        }
    }
}

static bool list_key(idunn_widget_t *w, const idunn_event_t *ev)
{
    list_t *l = w->data;
    bool shift = ev->mods & IDUNN_MOD_SHIFT;
    bool ctrl = ev->mods & IDUNN_MOD_CTRL;
    // a page: the rows the scroll view it is in can show (or 10)
    int page = 10;
    for (idunn_widget_t *p = w->parent; p; p = p->parent)
    {
        if (p->flags & IDUNN_CLIPS)
        {
            page = (p->rect.h / pitch(w) - 1) * columns(w);
            break;
        }
    }
    page = page < 1 ? 1 : page;
    int line = columns(w);
    switch (ev->code)
    {
    case IDUNN_KEY_UP:
        move_to(w, l->current < 0 ? 0 : l->current - line, shift);
        return true;
    case IDUNN_KEY_DOWN:
        move_to(w, l->current < 0 ? 0 : l->current + line, shift);
        return true;
    case IDUNN_KEY_LEFT:
    case IDUNN_KEY_RIGHT:
        if (!l->grid)
            return false;
        move_to(w, l->current < 0 ? 0 : l->current + (ev->code == IDUNN_KEY_LEFT ? -1 : 1), shift);
        return true;
    case IDUNN_KEY_PAGEUP:
        move_to(w, l->current - page, shift);
        return true;
    case IDUNN_KEY_PAGEDOWN:
        move_to(w, l->current + page, shift);
        return true;
    case IDUNN_KEY_HOME:
        move_to(w, 0, shift);
        return true;
    case IDUNN_KEY_END:
        move_to(w, l->count - 1, shift);
        return true;
    case IDUNN_KEY_ENTER:
        if (l->current >= 0 && l->on_activate)
            l->on_activate(w, l->activate_data);
        return l->current >= 0;
    case IDUNN_KEY_A:
        if (ctrl && l->multiple)
        {
            select_range(w, 0, l->count - 1);
            selection_changed(w);
            return true;
        }
        break;
    case IDUNN_KEY_SPACE:
        if (l->current >= 0 && !(l->find[0] && w->window->now - l->find_at <= FIND_MS))
        {
            if (ctrl && l->multiple)
                l->items[l->current].selected = !l->items[l->current].selected;
            else
                select_only(w, l->current);
            dirty_row(w, l->current);
            selection_changed(w);
            return true;
        }
        break;
    default:
        break;
    }
    if (!ctrl && ev->text && (unsigned char)ev->text[0] >= ' ')
    {
        find(w, ev->text);
        return true;
    }
    return false;
}

static bool list_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    list_t *l = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_KEY:
        return list_key(w, ev);
    case IDUNN_EV_MOTION:
    {
        int hover = item_at(w, ev->x, ev->y);
        if (hover != l->hover)
        {
            dirty_row(w, l->hover);
            l->hover = hover;
            dirty_row(w, hover);
        }
        return true;
    }
    case IDUNN_EV_PRESS:
    {
        int i = item_at(w, ev->x, ev->y);
        if (i < 0)
            return true;
        if (ev->clicks == 2 && !(ev->mods & (IDUNN_MOD_CTRL | IDUNN_MOD_SHIFT)))
        {
            if (l->on_activate)
                l->on_activate(w, l->activate_data);
            return true;
        }
        set_current(w, i);
        if ((ev->mods & IDUNN_MOD_CTRL) && l->multiple)
        {
            l->items[i].selected = !l->items[i].selected;
            l->anchor = i;
            dirty_row(w, i);
        }
        else if ((ev->mods & IDUNN_MOD_SHIFT) && l->multiple)
        {
            select_range(w, l->anchor, i);
        }
        else
        {
            l->anchor = i;
            select_only(w, i);
        }
        selection_changed(w);
        return true;
    }
    case IDUNN_EV_FOCUS:
    case IDUNN_EV_BLUR:
        dirty_row(w, l->current);
        return true;
    default:
        return false;
    }
}

static void free_items(list_t *l)
{
    for (int i = 0; i < l->count; i++)
    {
        free(l->items[i].icon);
        free(l->items[i].text);
        free(l->items[i].detail);
    }
    l->count = 0;
}

static void list_free(idunn_widget_t *w)
{
    list_t *l = w->data;
    free_items(l);
    free(l->items);
    free(l);
}

static const idunn_class_t list_class = {
    .name = "list view",
    .measure = list_measure,
    .draw = list_draw,
    .event = list_event,
    .free = list_free,
};

idunn_widget_t *idunn_list_view(void)
{
    list_t *l = calloc(1, sizeof(*l));
    idunn_widget_t *w =
        l ? idunn_widget_new(&list_class, l, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE) : NULL;
    if (!w)
    {
        free(l);
        return NULL;
    }
    l->current = -1;
    l->hover = -1;
    return w;
}

idunn_widget_t *idunn_grid_view(void)
{
    idunn_widget_t *w = idunn_list_view();
    if (w)
        ((list_t *)w->data)->grid = true;
    return w;
}

static list_t *as_list(const idunn_widget_t *w)
{
    return w && w->cls == &list_class ? w->data : NULL;
}

static char *copy_or_null(const char *s)
{
    return s ? strdup(s) : NULL;
}

int idunn_list_view_add(idunn_widget_t *list, const char *icon, const char *text,
                        const char *detail)
{
    list_t *l = as_list(list);
    if (!l)
        return -1;
    if (l->count == l->capacity)
    {
        int capacity = l->capacity ? l->capacity * 2 : 16;
        item_t *grown = realloc(l->items, (size_t)capacity * sizeof(item_t));
        if (!grown)
            return -1;
        l->items = grown;
        l->capacity = capacity;
    }
    item_t *it = &l->items[l->count];
    *it = (item_t){copy_or_null(icon), strdup(text ? text : ""), copy_or_null(detail), false};
    if (!it->text)
        return -1;
    idunn_relayout(list);
    idunn_dirty(list);
    return l->count++;
}

void idunn_list_view_clear(idunn_widget_t *list)
{
    list_t *l = as_list(list);
    if (!l)
        return;
    free_items(l);
    l->current = -1;
    l->anchor = 0;
    l->hover = -1;
    idunn_relayout(list);
    idunn_dirty(list);
}

int idunn_list_view_count(const idunn_widget_t *list)
{
    const list_t *l = as_list(list);
    return l ? l->count : 0;
}

void idunn_list_view_set_multiple(idunn_widget_t *list, bool multiple)
{
    list_t *l = as_list(list);
    if (l)
        l->multiple = multiple;
}

void idunn_list_view_set_framed(idunn_widget_t *list, bool framed)
{
    list_t *l = as_list(list);
    if (!l)
        return;
    l->framed = framed;
    idunn_dirty(list);
}

bool idunn_list_view_selected(const idunn_widget_t *list, int row)
{
    const list_t *l = as_list(list);
    return l && row >= 0 && row < l->count && l->items[row].selected;
}

void idunn_list_view_select(idunn_widget_t *list, int row, bool selected)
{
    list_t *l = as_list(list);
    if (!l || row < 0 || row >= l->count)
        return;
    if (selected && !l->multiple)
        select_only(list, row);
    l->items[row].selected = selected;
    dirty_row(list, row);
}

int idunn_list_view_current(const idunn_widget_t *list)
{
    const list_t *l = as_list(list);
    return l ? l->current : -1;
}

const char *idunn_list_view_text(const idunn_widget_t *list, int row)
{
    const list_t *l = as_list(list);
    return l && row >= 0 && row < l->count ? l->items[row].text : NULL;
}

void idunn_list_view_on_select(idunn_widget_t *list, idunn_callback_t on_select, void *data)
{
    list_t *l = as_list(list);
    if (!l)
        return;
    l->on_select = on_select;
    l->select_data = data;
}

void idunn_list_view_on_activate(idunn_widget_t *list, idunn_callback_t on_activate, void *data)
{
    list_t *l = as_list(list);
    if (!l)
        return;
    l->on_activate = on_activate;
    l->activate_data = data;
}
