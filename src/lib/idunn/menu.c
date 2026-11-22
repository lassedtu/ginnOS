/**
 * @file menu.c
 * @brief idunn: menus: a card in a popup (the card colour, a border, the
 *        radius, the md shadow, 4 pixels inside) with items of 32 pixels:
 *        an icon, the text and a shortcut in the muted colour; check items,
 *        separators and sub-menus.
 *
 * keys: Up and Down (past separators and disabled items), Home and End,
 * Enter and Space choose, Right opens a sub-menu, Left and Escape close a
 * menu, a letter goes to the next item that starts with it. the mouse:
 * the item under the pointer is the current one, a sub-menu opens after
 * 150 ms over its item, a click chooses.
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

#define PAD       4
#define ITEM      32
#define ITEM_PAD  8
#define ICON      16
#define GAP       8
#define SEPARATOR 9 // 1 px with 4 above and below
#define MIN_WIDTH 160
#define MARGIN    28 // room for the md shadow
#define SUB_MS    150

typedef enum
{
    ITEM_ACTION,
    ITEM_CHECK,
    ITEM_SEPARATOR,
    ITEM_SUB,
} kind_t;

typedef struct
{
    kind_t kind;
    char *icon;
    char *text;
    char *shortcut;
    bool checked;
    bool disabled;
    idunn_callback_t on_choose;
    void *data;
    idunn_menu_t *sub;
} item_t;

struct idunn_menu
{
    item_t *items;
    int count;
    int capacity;
    int32_t min_width; // design pixels
};

typedef struct
{
    idunn_menu_t *menu;
    idunn_widget_t *owner; // the widget the menu is for: its callbacks get it
    int current;           // -1: none
    int pressed;           // the item a press was on
    bool sub_open;
    void (*on_closed)(void *data);
    void *closed_data;
} view_t;

idunn_menu_t *idunn_menu_new(void)
{
    idunn_menu_t *m = calloc(1, sizeof(*m));
    if (m)
        m->min_width = MIN_WIDTH;
    return m;
}

void idunn_menu_free(idunn_menu_t *menu)
{
    if (!menu)
        return;
    for (int i = 0; i < menu->count; i++)
    {
        free(menu->items[i].icon);
        free(menu->items[i].text);
        free(menu->items[i].shortcut);
        idunn_menu_free(menu->items[i].sub);
    }
    free(menu->items);
    free(menu);
}

static item_t *add(idunn_menu_t *menu, kind_t kind, const char *icon, const char *text)
{
    if (!menu)
        return NULL;
    if (menu->count == menu->capacity)
    {
        int capacity = menu->capacity ? menu->capacity * 2 : 8;
        item_t *grown = realloc(menu->items, (size_t)capacity * sizeof(item_t));
        if (!grown)
            return NULL;
        menu->items = grown;
        menu->capacity = capacity;
    }
    item_t *it = &menu->items[menu->count++];
    *it = (item_t){.kind = kind};
    it->icon = icon ? strdup(icon) : NULL;
    it->text = strdup(text ? text : "");
    return it;
}

int idunn_menu_add(idunn_menu_t *menu, const char *icon, const char *text, const char *shortcut,
                   idunn_callback_t on_choose, void *data)
{
    item_t *it = add(menu, ITEM_ACTION, icon, text);
    if (!it)
        return -1;
    it->shortcut = shortcut ? strdup(shortcut) : NULL;
    it->on_choose = on_choose;
    it->data = data;
    return menu->count - 1;
}

int idunn_menu_add_check(idunn_menu_t *menu, const char *text, bool checked,
                         idunn_callback_t on_choose, void *data)
{
    item_t *it = add(menu, ITEM_CHECK, NULL, text);
    if (!it)
        return -1;
    it->checked = checked;
    it->on_choose = on_choose;
    it->data = data;
    return menu->count - 1;
}

void idunn_menu_add_separator(idunn_menu_t *menu)
{
    add(menu, ITEM_SEPARATOR, NULL, NULL);
}

int idunn_menu_add_sub(idunn_menu_t *menu, const char *icon, const char *text, idunn_menu_t *sub)
{
    item_t *it = add(menu, ITEM_SUB, icon, text);
    if (!it)
    {
        idunn_menu_free(sub);
        return -1;
    }
    it->sub = sub;
    return menu->count - 1;
}

void idunn_menu_set_disabled(idunn_menu_t *menu, int item, bool disabled)
{
    if (menu && item >= 0 && item < menu->count)
        menu->items[item].disabled = disabled;
}

bool idunn_menu_checked(const idunn_menu_t *menu, int item)
{
    return menu && item >= 0 && item < menu->count && menu->items[item].checked;
}

void idunn_menu_set_checked(idunn_menu_t *menu, int item, bool checked)
{
    if (menu && item >= 0 && item < menu->count)
        menu->items[item].checked = checked;
}

void idunn_menu_set_min_width(idunn_menu_t *menu, int32_t width)
{
    if (menu)
        menu->min_width = width;
}

static bool has_icons(const idunn_menu_t *menu)
{
    for (int i = 0; i < menu->count; i++)
    {
        if (menu->items[i].icon || menu->items[i].kind == ITEM_CHECK)
            return true;
    }
    return false;
}

static int32_t item_height(const idunn_theme_t *t, const item_t *it)
{
    return idunn_px(t, it->kind == ITEM_SEPARATOR ? SEPARATOR : ITEM);
}

static gfx_rect_t item_rect(idunn_widget_t *w, int i)
{
    const view_t *v = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t pad = idunn_px(t, PAD);
    int32_t y = w->rect.y + pad;
    for (int k = 0; k < i; k++)
        y += item_height(t, &v->menu->items[k]);
    return gfx_rect(w->rect.x + pad, y, w->rect.w - 2 * pad, item_height(t, &v->menu->items[i]));
}

static int item_at(idunn_widget_t *w, int32_t x, int32_t y)
{
    const view_t *v = w->data;
    for (int i = 0; i < v->menu->count; i++)
    {
        if (gfx_rect_contains(item_rect(w, i), x, y))
            return i;
    }
    return -1;
}

static bool choosable(const item_t *it)
{
    return it->kind != ITEM_SEPARATOR && !it->disabled;
}

static void view_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const view_t *v = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    bragi_face_t *body = idunn_face(ctx, IDUNN_TEXT_BODY);
    bragi_face_t *small = idunn_face(ctx, IDUNN_TEXT_SMALL);
    int32_t icons = has_icons(v->menu) ? idunn_px(t, ICON + GAP) : 0;
    int32_t widest = 0;
    *height = 2 * idunn_px(t, PAD);
    for (int i = 0; i < v->menu->count; i++)
    {
        const item_t *it = &v->menu->items[i];
        *height += item_height(t, it);
        if (it->kind == ITEM_SEPARATOR)
            continue;
        int32_t iw = icons + (body ? bragi_measure(body, it->text, -1) : 0);
        if (it->shortcut && small)
            iw += idunn_px(t, 24) + bragi_measure(small, it->shortcut, -1);
        if (it->kind == ITEM_SUB)
            iw += idunn_px(t, 24 + ICON);
        widest = iw > widest ? iw : widest;
    }
    *width = widest + 2 * idunn_px(t, PAD + ITEM_PAD);
    int32_t least = idunn_px(t, v->menu->min_width);
    *width = *width < least ? least : *width;
}

static void draw_item(idunn_widget_t *w, gfx_buffer_t *buf, int i, bool icons)
{
    const view_t *v = w->data;
    const item_t *it = &v->menu->items[i];
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_rect_t r = item_rect(w, i);
    if (it->kind == ITEM_SEPARATOR)
    {
        int32_t one = idunn_px(t, 1);
        int32_t pad = idunn_px(t, PAD);
        gfx_fill_rect(buf, gfx_rect(r.x - pad, r.y + (r.h - one) / 2, r.w + 2 * pad, one),
                      idunn_color(t, IDUNN_COLOR_BORDER));
        return;
    }
    if (i == v->current && !it->disabled)
        gfx_fill_round_rect(buf, r, idunn_radius_sm(t), idunn_color(t, IDUNN_COLOR_HOVER));
    gfx_color_t fg = idunn_color(t, IDUNN_COLOR_FOREGROUND);
    gfx_color_t muted = idunn_color(t, IDUNN_COLOR_MUTED_FOREGROUND);
    if (it->disabled)
        fg = muted;
    int32_t x = r.x + idunn_px(t, ITEM_PAD);
    int32_t right = r.x + r.w - idunn_px(t, ITEM_PAD);
    int32_t size = idunn_px(t, ICON);
    if (icons)
    {
        const char *name = it->kind == ITEM_CHECK ? (it->checked ? "check" : NULL) : it->icon;
        const icon_t *icon = name ? idunn_icon(ctx, name, ICON) : NULL;
        if (icon)
            icon_draw_mask(buf, icon, x, r.y + (r.h - size) / 2,
                           it->kind == ITEM_CHECK ? fg : muted);
        x += size + idunn_px(t, GAP);
    }
    if (it->kind == ITEM_SUB)
    {
        const icon_t *chevron = idunn_icon(ctx, "chevron-right", ICON);
        if (chevron)
            icon_draw_mask(buf, chevron, right - size, r.y + (r.h - size) / 2, muted);
        right -= size + idunn_px(t, GAP);
    }
    if (it->shortcut)
    {
        bragi_face_t *small = idunn_face(ctx, IDUNN_TEXT_SMALL);
        int32_t sw = small ? bragi_measure(small, it->shortcut, -1) : 0;
        int32_t line = idunn_text_style(t, IDUNN_TEXT_SMALL).line_height;
        idunn_draw_text(ctx, buf, IDUNN_TEXT_SMALL, right - sw, r.y + (r.h - line) / 2, 0,
                        it->shortcut, muted);
        right -= sw + idunn_px(t, GAP);
    }
    int32_t line = idunn_text_style(t, IDUNN_TEXT_BODY).line_height;
    idunn_draw_text(ctx, buf, IDUNN_TEXT_BODY, x, r.y + (r.h - line) / 2, right - x, it->text, fg);
}

static void view_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const view_t *v = w->data;
    const idunn_theme_t *t = idunn_theme(w);
    int32_t radius = idunn_radius(t);
    idunn_draw_shadow(t, buf, w->rect, radius, IDUNN_SHADOW_MD);
    gfx_fill_round_rect(buf, w->rect, radius, idunn_color(t, IDUNN_COLOR_CARD));
    gfx_border_round_rect(buf, w->rect, radius, idunn_px(t, 1), idunn_color(t, IDUNN_COLOR_BORDER));
    bool icons = has_icons(v->menu);
    for (int i = 0; i < v->menu->count; i++)
        draw_item(w, buf, i, icons);
}

static void set_current(idunn_widget_t *w, int i)
{
    view_t *v = w->data;
    if (v->current == i)
        return;
    if (v->current >= 0)
        idunn_dirty_rect(w, item_rect(w, v->current));
    v->current = i;
    if (i >= 0)
        idunn_dirty_rect(w, item_rect(w, i));
}

static idunn_window_t *open_view(idunn_menu_t *menu, idunn_widget_t *owner, idunn_widget_t *from,
                                 gfx_rect_t anchor, idunn_side_t side, int current);

static void open_sub(idunn_widget_t *w, bool keyboard)
{
    view_t *v = w->data;
    const item_t *it = &v->menu->items[v->current];
    if (it->kind != ITEM_SUB || !it->sub)
        return;
    // next to this card, its first item level with this item
    int32_t pad = idunn_px(idunn_theme(w), PAD);
    gfx_rect_t r = item_rect(w, v->current);
    gfx_rect_t anchor = gfx_rect(w->rect.x, r.y - pad, w->rect.w, r.h);
    int first = -1;
    for (int i = 0; keyboard && i < it->sub->count && first < 0; i++)
        first = choosable(&it->sub->items[i]) ? i : -1;
    if (open_view(it->sub, v->owner, w, anchor, IDUNN_SIDE_RIGHT, first))
        v->sub_open = true;
}

static void close_sub(idunn_widget_t *w)
{
    view_t *v = w->data;
    if (v->sub_open && w->window->popup)
        idunn_popup_close(w->window->popup);
    v->sub_open = false;
}

/** choose item i: a sub-menu opens; anything else closes all the menus first */
static void choose(idunn_widget_t *w, int i, bool keyboard)
{
    view_t *v = w->data;
    if (i < 0 || i >= v->menu->count || !choosable(&v->menu->items[i]))
        return;
    set_current(w, i);
    item_t *it = &v->menu->items[i];
    if (it->kind == ITEM_SUB)
    {
        open_sub(w, keyboard);
        return;
    }
    if (it->kind == ITEM_CHECK)
        it->checked = !it->checked;
    idunn_callback_t call = it->on_choose;
    void *data = it->data;
    idunn_widget_t *owner = v->owner;
    idunn_popup_close_all(w->window);
    if (call)
        call(owner, data);
}

/** the next choosable item from i in a direction, going round */
static int step(const view_t *v, int i, int dir)
{
    for (int n = 0; n < v->menu->count; n++)
    {
        i = (i + dir + v->menu->count) % v->menu->count;
        if (choosable(&v->menu->items[i]))
            return i;
    }
    return -1;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool view_key(idunn_widget_t *w, const idunn_event_t *ev)
{
    view_t *v = w->data;
    switch (ev->code)
    {
    case IDUNN_KEY_DOWN:
        set_current(w, step(v, v->current < 0 ? -1 : v->current, 1));
        return true;
    case IDUNN_KEY_UP:
        set_current(w, step(v, v->current < 0 ? 0 : v->current, -1));
        return true;
    case IDUNN_KEY_HOME:
        set_current(w, step(v, -1, 1));
        return true;
    case IDUNN_KEY_END:
        set_current(w, step(v, 0, -1));
        return true;
    case IDUNN_KEY_ENTER:
    case IDUNN_KEY_SPACE:
        choose(w, v->current, true);
        return true;
    case IDUNN_KEY_RIGHT:
        if (v->current >= 0 && v->menu->items[v->current].kind == ITEM_SUB)
            open_sub(w, true);
        return true;
    case IDUNN_KEY_LEFT:
    case IDUNN_KEY_ESCAPE:
        // Left closes a sub-menu only; Escape any menu
        if (ev->code == IDUNN_KEY_ESCAPE || (w->window->opener && w->window->opener->opener))
            idunn_popup_close(w->window);
        return true;
    default:
        break;
    }
    // a letter: the next item that starts with it
    if (ev->text && ev->text[0] > ' ')
    {
        for (int n = 1; n <= v->menu->count; n++)
        {
            int i = (v->current + n + v->menu->count) % v->menu->count;
            const item_t *it = &v->menu->items[i];
            if (choosable(it) && lower(it->text[0]) == lower(ev->text[0]))
            {
                set_current(w, i);
                break;
            }
        }
        return true;
    }
    return false;
}

static bool view_event(idunn_widget_t *w, const idunn_event_t *ev)
{
    view_t *v = w->data;
    switch (ev->type)
    {
    case IDUNN_EV_KEY:
        return view_key(w, ev);
    case IDUNN_EV_MOTION:
    {
        int i = item_at(w, ev->x, ev->y);
        if (i >= 0 && !choosable(&v->menu->items[i]))
            i = -1;
        if (i == v->current)
            return true;
        set_current(w, i);
        close_sub(w);
        // a sub-menu opens when the pointer stays on its item
        if (i >= 0 && v->menu->items[i].kind == ITEM_SUB)
            idunn_wake(w, w->window->now + SUB_MS);
        return true;
    }
    case IDUNN_EV_TIMER:
        if (v->current >= 0 && v->menu->items[v->current].kind == ITEM_SUB && !v->sub_open)
            open_sub(w, false);
        return true;
    case IDUNN_EV_PRESS:
        v->pressed = item_at(w, ev->x, ev->y);
        return true;
    case IDUNN_EV_RELEASE:
    {
        int i = item_at(w, ev->x, ev->y);
        if (i >= 0 && i == v->pressed)
            choose(w, i, false);
        return true;
    }
    default:
        return false;
    }
}

static void view_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t view_class = {
    .name = "menu",
    .measure = view_measure,
    .draw = view_draw,
    .event = view_event,
    .free = view_free,
};

static void view_closed(idunn_window_t *popup)
{
    // the menu that opened this one has no sub-menu open now
    idunn_window_t *opener = popup->opener;
    if (opener && opener->root && opener->root->cls == &view_class)
        ((view_t *)opener->root->data)->sub_open = false;
    view_t *v = popup->root ? popup->root->data : NULL;
    if (v && v->on_closed)
        v->on_closed(v->closed_data);
}

static idunn_window_t *open_view(idunn_menu_t *menu, idunn_widget_t *owner, idunn_widget_t *from,
                                 gfx_rect_t anchor, idunn_side_t side, int current)
{
    view_t *v = calloc(1, sizeof(*v));
    idunn_widget_t *w =
        v ? idunn_widget_new(&view_class, v, IDUNN_FOCUSABLE | IDUNN_INTERACTIVE) : NULL;
    if (!w)
    {
        free(v);
        return NULL;
    }
    v->menu = menu;
    v->owner = owner;
    v->current = current;
    v->pressed = -1;
    int32_t margin = idunn_px(idunn_theme(from), MARGIN);
    idunn_window_t *popup = idunn_popup_open(from, w, anchor, side, margin, false);
    if (popup)
    {
        popup->closed = view_closed;
        idunn_focus(w);
    }
    return popup;
}

idunn_window_t *idunn_menu_open(idunn_menu_t *menu, idunn_widget_t *owner, gfx_rect_t anchor,
                                int current, void (*on_closed)(void *data), void *data)
{
    if (!menu || !owner || !owner->window)
        return NULL;
    idunn_window_t *popup = open_view(menu, owner, owner, anchor, IDUNN_SIDE_BELOW, current);
    if (popup)
    {
        view_t *v = popup->root->data;
        v->on_closed = on_closed;
        v->closed_data = data;
    }
    return popup;
}

void idunn_menu_popup(idunn_menu_t *menu, idunn_widget_t *owner)
{
    if (!owner)
        return;
    // 4 pixels under the widget
    gfx_rect_t r = owner->rect;
    r.h += idunn_px(idunn_theme(owner), 4);
    // opened with the keyboard: the first item is the current one
    int first = -1;
    if (owner->window && owner->window->focus_visible)
    {
        for (int i = 0; menu && i < menu->count && first < 0; i++)
            first = choosable(&menu->items[i]) ? i : -1;
    }
    idunn_menu_open(menu, owner, r, first, NULL, NULL);
}
