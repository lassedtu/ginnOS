/**
 * @file window.c
 * @brief windows: the z order, the focus, the decorations, hit testing.
 *
 * a decorated window is a title bar (drawn by the server, kept in a buffer
 * until it changes) above the client's content, with round corners, a
 * thin rim and a soft shadow. the shadow is a picture made once for a
 * small window and stretched to each window (nine parts: the corners as
 * they are, the edges repeated).
 */

#include "heimdalld.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// the look, made from the theme by window_look
static gfx_buffer_t *shadow; // the shadow of a small window, stretched to each window
static struct
{
    int32_t left; // how far the shadow reaches out from the frame
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t x; // the column and the row of the picture that are stretched
    int32_t y;
} reach;
static uint8_t *corner; // the coverage of a round bottom left corner
static int32_t radius;  // of the window corners

/** the colour of a theme token */
static gfx_color_t token(idunn_color_t which)
{
    return idunn_color(&S.theme, which);
}

void window_look(void)
{
    // the title, the buttons and the pointer at the scale
    int32_t scale = S.theme.scale;
    bragi_face_free(S.title_face);
    S.title_face = S.title_font ? bragi_face_new(S.title_font, 13.0f * (float)scale) : NULL;
    icon_t **icons[] = {&S.icon_minimize, &S.icon_maximize, &S.icon_restore, &S.icon_close};
    const char *names[] = {"minus", "square", "copy", "x"};
    icon_set_t *lucide = icon_set_named("lucide");
    for (int i = 0; i < 4; i++)
    {
        icon_free(*icons[i]);
        *icons[i] = lucide ? icon_load(lucide, names[i], 16, scale) : NULL;
    }
    icon_set_close(lucide);
    cursor_init(scale);
    S.cursor_rect = cursor_rect(S.cursor, S.px, S.py);

    radius = idunn_radius_lg(&S.theme);

    // the shadow: the theme's window shadow around a window large enough
    // that its middle row and column are straight
    gfx_shadow_t layers[IDUNN_SHADOW_LAYERS];
    int count = idunn_shadow(&S.theme, IDUNN_SHADOW_WINDOW, layers);
    reach.left = reach.top = reach.right = reach.bottom = 0;
    int32_t most = 0;
    for (int i = 0; i < count; i++)
    {
        // a blur reaches its radius and a little more (the blur's tail)
        int32_t r = layers[i].blur + layers[i].blur / 4 + layers[i].spread;
        reach.left = r - layers[i].x > reach.left ? r - layers[i].x : reach.left;
        reach.right = r + layers[i].x > reach.right ? r + layers[i].x : reach.right;
        reach.top = r - layers[i].y > reach.top ? r - layers[i].y : reach.top;
        reach.bottom = r + layers[i].y > reach.bottom ? r + layers[i].y : reach.bottom;
        most = layers[i].blur + layers[i].spread > most ? layers[i].blur + layers[i].spread : most;
    }
    int32_t side = 2 * (radius + most) + 2;
    gfx_buffer_free(shadow);
    shadow = gfx_buffer_new(reach.left + side + reach.right, reach.top + side + reach.bottom);
    if (shadow)
    {
        gfx_rect_t box = gfx_rect(reach.left, reach.top, side, side);
        for (int i = 0; i < count; i++)
            gfx_box_shadow(shadow, box, radius, &layers[i]);
        // nothing under the window itself (an ALPHA window shows what is below)
        gfx_buffer_t *hole = gfx_buffer_new(side, side);
        if (hole)
        {
            gfx_fill_round_rect(hole, gfx_rect(0, 0, side, side), radius, gfx_rgb(255, 255, 255));
            for (int32_t y = 0; y < side; y++)
                for (int32_t x = 0; x < side; x++)
                {
                    uint32_t *p = gfx_pixel_at(shadow, reach.left + x, reach.top + y);
                    *p = gfx_fade(*p, (uint8_t)(255 - gfx_alpha(*gfx_pixel_at(hole, x, y))));
                }
            gfx_buffer_free(hole);
        }
    }
    reach.x = reach.left + side / 2;
    reach.y = reach.top + side / 2;

    // the coverage of a round corner
    free(corner);
    corner = radius > 0 ? malloc((size_t)(radius * radius)) : NULL;
    gfx_buffer_t *disc = corner ? gfx_buffer_new(2 * radius, 2 * radius) : NULL;
    if (disc)
    {
        gfx_fill_round_rect(disc, gfx_rect(0, 0, 2 * radius, 2 * radius), radius,
                            gfx_rgb(255, 255, 255));
        for (int32_t y = 0; y < radius; y++)
            for (int32_t x = 0; x < radius; x++)
                corner[y * radius + x] = gfx_alpha(*gfx_pixel_at(disc, x, radius + y));
        gfx_buffer_free(disc);
    }
    else
    {
        free(corner);
        corner = NULL;
        radius = 0;
    }

    // every title bar again
    for (window_t *w = S.bottom; w; w = w->above)
        w->bar_dirty = true;
    screen_damage(gfx_rect(0, 0, S.width, S.height));
}

bool window_decorated(const window_t *w)
{
    return !(w->flags & HEIM_WINDOW_UNDECORATED);
}

static int32_t bar_height(const window_t *w)
{
    return window_decorated(w) ? TITLE_HEIGHT : 0;
}

gfx_rect_t window_frame(const window_t *w)
{
    return gfx_rect(w->x, w->y, w->content.width, w->content.height + bar_height(w));
}

gfx_rect_t window_content(const window_t *w)
{
    return gfx_rect(w->x, w->y + bar_height(w), w->content.width, w->content.height);
}

gfx_rect_t window_bounds(const window_t *w)
{
    gfx_rect_t f = window_frame(w);
    if (!window_decorated(w))
        return f;
    int32_t left = reach.left > RESIZE_BAND ? reach.left : RESIZE_BAND;
    int32_t top = reach.top > RESIZE_BAND ? reach.top : RESIZE_BAND;
    int32_t right = reach.right > RESIZE_BAND ? reach.right : RESIZE_BAND;
    int32_t bottom = reach.bottom > RESIZE_BAND ? reach.bottom : RESIZE_BAND;
    return gfx_rect(f.x - left, f.y - top, f.w + left + right, f.h + top + bottom);
}

bool window_visible(const window_t *w)
{
    return w->shown && !w->minimized && w->content.pixels &&
           (!w->parent || window_visible(w->parent));
}

bool window_is_popup(const window_t *w)
{
    return w->flags & HEIM_WINDOW_POPUP;
}

window_t *window_root(window_t *w)
{
    while (w && w->parent)
        w = w->parent;
    return w;
}

void window_damage(window_t *w)
{
    if (window_visible(w))
        screen_damage(window_bounds(w));
}

static bool rounded(const window_t *w)
{
    return window_decorated(w) && !w->maximized;
}

/**
 * where a title bar button is, in the bar (0: close, 1: maximize or
 * minimize, 2: minimize). a window that cannot be resized has no
 * maximize button.
 */
static gfx_rect_t button_rect(const window_t *w, part_t part)
{
    int32_t slot = 0;
    if (part == PART_MAXIMIZE)
        slot = 1;
    else if (part == PART_MINIMIZE)
        slot = (w->flags & HEIM_WINDOW_RESIZABLE) ? 2 : 1;
    int32_t x = w->content.width - idunn_px(&S.theme, 6) - (slot + 1) * BUTTON_WIDTH;
    return gfx_rect(x, (TITLE_HEIGHT - BUTTON_HEIGHT) / 2, BUTTON_WIDTH, BUTTON_HEIGHT);
}

static bool has_button(const window_t *w, part_t part)
{
    return part != PART_MAXIMIZE || (w->flags & HEIM_WINDOW_RESIZABLE);
}

/**
 * a window button as a ghost button: the muted colour, the hover colour
 * under the pointer (the destructive colour for close), a little darker
 * while pressed.
 */
static void draw_button(window_t *w, part_t part, const icon_t *icon, bool focused)
{
    gfx_rect_t r = button_rect(w, part);
    gfx_color_t ink = token(IDUNN_COLOR_MUTED_FOREGROUND);
    if (!focused)
        ink = gfx_fade(ink, 160);
    if (w->hover == part || w->pressed == part)
    {
        bool close = part == PART_CLOSE;
        gfx_color_t back = token(close ? IDUNN_COLOR_DESTRUCTIVE : IDUNN_COLOR_HOVER);
        if (w->pressed == part)
            back = gfx_mix(back, close ? gfx_rgb(0, 0, 0) : token(IDUNN_COLOR_FOREGROUND), 36);
        gfx_fill_round_rect(w->bar, r, idunn_radius_sm(&S.theme), back);
        ink = close ? gfx_rgb(255, 255, 255) : token(IDUNN_COLOR_FOREGROUND);
    }
    if (icon)
        icon_draw_mask(w->bar, icon, r.x + (r.w - icon->width) / 2,
                       r.y + (r.h - icon->height) / 2, ink);
}

static void draw_bar(window_t *w)
{
    int32_t width = w->content.width;
    if (!w->bar || w->bar->width != width)
    {
        gfx_buffer_free(w->bar);
        w->bar = gfx_buffer_new(width, TITLE_HEIGHT);
        if (!w->bar)
            return;
    }
    // a window with a menu open looks focused
    bool focused = S.focus && window_root(S.focus) == w;
    gfx_clear(w->bar, GFX_TRANSPARENT);
    // the preview's bar: a soft gradient, a light line at the top, the
    // border colour below
    int32_t r = rounded(w) ? radius : 0;
    gfx_fill_round_rect_gradient(w->bar, gfx_rect(0, 0, width, TITLE_HEIGHT + r), r,
                                 token(IDUNN_COLOR_TITLE_TOP), token(IDUNN_COLOR_TITLE_BOTTOM));
    gfx_rect_t clip = w->bar->clip;
    gfx_set_clip(w->bar, gfx_rect(0, 0, width, 1));
    gfx_border_round_rect(w->bar, gfx_rect(0, 0, width, TITLE_HEIGHT + r), r, 1,
                          token(IDUNN_COLOR_TOP_LIGHT));
    gfx_set_clip(w->bar, clip);
    gfx_fill_rect(w->bar, gfx_rect(0, TITLE_HEIGHT - 1, width, 1), token(IDUNN_COLOR_BORDER));

    // the title, in the middle, between the buttons
    int32_t buttons = idunn_px(&S.theme, 6 + 8) + 3 * BUTTON_WIDTH;
    if (S.title_face && width > 2 * buttons)
    {
        bragi_face_t *f = S.title_face;
        int32_t lh = bragi_line_height(f);
        gfx_rect_t box = gfx_rect(buttons, (TITLE_HEIGHT - lh) / 2 + 1, width - 2 * buttons, lh);
        bragi_draw_box(w->bar, f, box, w->title, BRAGI_ALIGN_CENTER,
                       token(focused ? IDUNN_COLOR_FOREGROUND : IDUNN_COLOR_MUTED_FOREGROUND));
    }

    draw_button(w, PART_MINIMIZE, S.icon_minimize, focused);
    if (has_button(w, PART_MAXIMIZE))
        draw_button(w, PART_MAXIMIZE, w->maximized ? S.icon_restore : S.icon_maximize, focused);
    draw_button(w, PART_CLOSE, S.icon_close, focused);
    w->bar_dirty = false;
}

/**
 * blend a part of the shadow picture over the screen. a part one pixel
 * wide or high is repeated across dst (the stretched edges).
 */
static void shadow_part(gfx_rect_t dst, gfx_rect_t src, gfx_rect_t clip)
{
    gfx_rect_t area = gfx_rect_intersect(dst, clip);
    for (int32_t y = area.y; y < area.y + area.h; y++)
    {
        int32_t sy = src.y + (src.h == 1 ? 0 : y - dst.y);
        uint32_t *p = gfx_pixel_at(S.screen, 0, y);
        for (int32_t x = area.x; x < area.x + area.w; x++)
        {
            int32_t sx = src.x + (src.w == 1 ? 0 : x - dst.x);
            uint32_t s = *gfx_pixel_at(shadow, sx, sy);
            if (s)
                p[x] = gfx_over(p[x], s);
        }
    }
}

static void draw_shadow(gfx_rect_t f, gfx_rect_t clip)
{
    if (!shadow)
        return;
    const int32_t left = reach.x;                   // columns before the stretched one
    const int32_t right = shadow->width - left - 1; // columns after it
    const int32_t top = reach.y;
    const int32_t bottom = shadow->height - top - 1;
    int32_t x0 = f.x - reach.left;
    int32_t y0 = f.y - reach.top;
    int32_t x1 = f.x + f.w + reach.right; // the right end
    int32_t y1 = f.y + f.h + reach.bottom;
    int32_t mid_w = (x1 - right) - (x0 + left);
    int32_t mid_h = (y1 - bottom) - (y0 + top);
    if (mid_w < 0 || mid_h < 0)
        return; // smaller than the picture: no shadow

    int32_t sx_right = left + 1;
    int32_t sy_bottom = top + 1;
    // the corners
    shadow_part(gfx_rect(x0, y0, left, top), gfx_rect(0, 0, left, top), clip);
    shadow_part(gfx_rect(x1 - right, y0, right, top), gfx_rect(sx_right, 0, right, top), clip);
    shadow_part(gfx_rect(x0, y1 - bottom, left, bottom), gfx_rect(0, sy_bottom, left, bottom),
                clip);
    shadow_part(gfx_rect(x1 - right, y1 - bottom, right, bottom),
                gfx_rect(sx_right, sy_bottom, right, bottom), clip);
    // the edges (the middle is under the window)
    shadow_part(gfx_rect(x0 + left, y0, mid_w, top), gfx_rect(left, 0, 1, top), clip);
    shadow_part(gfx_rect(x0 + left, y1 - bottom, mid_w, bottom),
                gfx_rect(left, sy_bottom, 1, bottom), clip);
    shadow_part(gfx_rect(x0, y0 + top, left, mid_h), gfx_rect(0, top, left, 1), clip);
    shadow_part(gfx_rect(x1 - right, y0 + top, right, mid_h), gfx_rect(sx_right, top, right, 1),
                clip);
}

/**
 * the content, with round bottom corners when the window has them.
 */
static void draw_content(window_t *w, gfx_rect_t clip)
{
    gfx_rect_t c = window_content(w);
    bool alpha = w->flags & HEIM_WINDOW_ALPHA;
    int32_t r = rounded(w) && corner && c.w >= 2 * radius && c.h >= radius ? radius : 0;
    gfx_rect_t body = gfx_rect(0, 0, c.w, c.h - r);
    gfx_rect_t middle = gfx_rect(r, c.h - r, c.w - 2 * r, r);
    if (alpha)
    {
        gfx_blend(S.screen, c.x, c.y, &w->content, body, 255);
        gfx_blend(S.screen, c.x + r, c.y + c.h - r, &w->content, middle, 255);
    }
    else
    {
        gfx_copy(S.screen, c.x, c.y, &w->content, body);
        gfx_copy(S.screen, c.x + r, c.y + c.h - r, &w->content, middle);
    }

    // the two corners, pixel by pixel with their coverage
    for (int32_t y = 0; y < r; y++)
    {
        int32_t sy = c.y + c.h - r + y;
        if (sy < clip.y || sy >= clip.y + clip.h)
            continue;
        for (int32_t x = 0; x < r; x++)
        {
            uint8_t cover = corner[y * r + x];
            if (cover == 0)
                continue;
            for (int side = 0; side < 2; side++)
            {
                int32_t cx = side == 0 ? x : c.w - 1 - x;
                int32_t sx = c.x + cx;
                if (sx < clip.x || sx >= clip.x + clip.w)
                    continue;
                uint32_t src = *gfx_pixel_at(&w->content, cx, c.h - r + y);
                uint32_t *dst = gfx_pixel_at(S.screen, sx, sy);
                if (cover == 255 && !alpha)
                    *dst = src;
                else
                    *dst = gfx_over(*dst, gfx_fade(alpha ? src : (src | 0xFF000000u), cover));
            }
        }
    }
}

void window_draw(window_t *w, gfx_rect_t clip)
{
    gfx_rect_t f = window_frame(w);
    if (window_decorated(w))
    {
        if (!w->maximized)
            draw_shadow(f, clip);
        if (w->bar_dirty || !w->bar)
            draw_bar(w);
        if (w->bar)
            gfx_blend(S.screen, f.x, f.y, w->bar, gfx_rect(0, 0, w->bar->width, TITLE_HEIGHT),
                      255);
    }
    draw_content(w, clip);
}

window_t *window_new(client_t *client, uint32_t id, uint32_t flags, const char *title,
                     window_t *parent)
{
    window_t *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->client = client;
    w->id = id;
    w->flags = flags & (HEIM_WINDOW_RESIZABLE | HEIM_WINDOW_ALPHA | HEIM_WINDOW_UNDECORATED |
                        HEIM_WINDOW_POPUP | HEIM_WINDOW_TOOLTIP);
    if (parent)
    {
        w->flags = (w->flags & HEIM_WINDOW_TOOLTIP) | HEIM_WINDOW_POPUP | HEIM_WINDOW_ALPHA |
                   HEIM_WINDOW_UNDECORATED;
        w->parent = parent;
    }
    else
    {
        w->flags &= ~(uint32_t)(HEIM_WINDOW_POPUP | HEIM_WINDOW_TOOLTIP);
    }
    memcpy(w->title, title, HEIM_TITLE_MAX);
    w->title[HEIM_TITLE_MAX - 1] = '\0';
    w->bar_dirty = true;

    // on top
    w->below = S.top;
    if (S.top)
        S.top->above = w;
    else
        S.bottom = w;
    S.top = w;
    return w;
}

static void unlink_window(window_t *w)
{
    if (w->below)
        w->below->above = w->above;
    else
        S.bottom = w->above;
    if (w->above)
        w->above->below = w->below;
    else
        S.top = w->below;
    w->above = NULL;
    w->below = NULL;
}

void window_destroy(window_t *w)
{
    window_damage(w);
    unlink_window(w);
    // its popups go with it: hidden now, freed when their client says so
    for (window_t *o = S.bottom; o; o = o->above)
    {
        if (o->parent == w)
        {
            heim_msg_t msg = {0};
            msg.type = HEIM_EV_CLOSE;
            client_send_window(o, &msg);
            o->parent = NULL;
            o->shown = false;
        }
    }
    window_t *parent = w->parent;
    if (S.pointer_window == w)
        S.pointer_window = NULL;
    if (S.grab == w)
        S.grab = NULL;
    if (S.drag_window == w)
    {
        if (S.drag == DRAG_RESIZE)
            screen_damage(S.drag_rect);
        S.drag = DRAG_NONE;
        S.drag_window = NULL;
    }
    if (S.last_click_window == w)
        S.last_click_window = NULL;
    if (w->map)
        munmap(w->map, w->map_size);
    gfx_buffer_free(w->own);
    if (w->pending_map)
        munmap(w->pending_map, w->pending_size);
    gfx_buffer_free(w->bar);
    bool had_focus = S.focus == w;
    if (had_focus)
        S.focus = NULL;
    free(w);
    if (had_focus && parent && window_visible(parent))
        window_focus(parent);
    else if (had_focus)
        window_focus_top();
    input_pointer_update();
}

window_t *window_find(client_t *client, uint32_t id)
{
    for (window_t *w = S.bottom; w; w = w->above)
        if (w->client == client && w->id == id)
            return w;
    return NULL;
}

static void to_top(window_t *w)
{
    if (S.top == w)
        return;
    unlink_window(w);
    w->below = S.top;
    if (S.top)
        S.top->above = w;
    else
        S.bottom = w;
    S.top = w;
    window_damage(w);
}

void window_raise(window_t *w)
{
    to_top(w);
    if (w->parent)
        return;
    // its popups stay above it, in their order
    enum { MAX = 16 };
    window_t *popups[MAX];
    int count = 0;
    for (window_t *o = S.bottom; o && count < MAX; o = o->above)
    {
        if (o->parent && window_root(o) == w)
            popups[count++] = o;
    }
    for (int i = 0; i < count; i++)
        to_top(popups[i]);
}

static void send_focus(window_t *w, bool focused)
{
    heim_msg_t msg = {0};
    msg.type = HEIM_EV_FOCUS;
    msg.focus.focused = focused;
    client_send_window(w, &msg);
}

void window_focus(window_t *w)
{
    if (S.focus == w)
        return;
    if (S.focus)
    {
        window_t *root = window_root(S.focus);
        root->bar_dirty = true;
        window_damage(root);
        send_focus(S.focus, false);
    }
    S.focus = w;
    if (w)
    {
        window_t *root = window_root(w);
        root->bar_dirty = true;
        window_damage(root);
        send_focus(w, true);
    }
}

void window_focus_top(void)
{
    for (window_t *w = S.top; w; w = w->below)
    {
        if (window_visible(w) && !(w->flags & HEIM_WINDOW_TOOLTIP))
        {
            window_focus(w);
            return;
        }
    }
    window_focus(NULL);
}

/**
 * the first time a window shows: place it (a cascade from the middle) and
 * give it the focus.
 */
void window_place(window_t *w)
{
    if (!w->parent || !w->content.pixels)
        return;
    gfx_rect_t pc = window_content(w->parent);
    gfx_rect_t a = gfx_rect(pc.x + w->anchor.x, pc.y + w->anchor.y, w->anchor.w, w->anchor.h);
    int32_t m = w->margin;
    int32_t cw = w->content.width - 2 * m;
    int32_t ch = w->content.height - 2 * m;
    int32_t x;
    int32_t y;
    switch (w->side)
    {
    case HEIM_SIDE_ABOVE:
        x = a.x + (a.w - cw) / 2;
        y = a.y - ch;
        if (y < 0)
            y = a.y + a.h;
        break;
    case HEIM_SIDE_RIGHT:
        x = a.x + a.w;
        y = a.y;
        if (x + cw > S.width)
            x = a.x - cw;
        break;
    default:
        x = a.x;
        y = a.y + a.h;
        if (y + ch > S.height && a.y - ch >= 0)
            y = a.y - ch;
        break;
    }
    // inside the screen
    x = x + cw > S.width ? S.width - cw : x;
    y = y + ch > S.height ? S.height - ch : y;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    window_damage(w);
    w->x = x - m;
    w->y = y - m;
    window_damage(w);
}

bool window_close_popups(void)
{
    bool taking = false;
    for (window_t *w = S.bottom; w; w = w->above)
    {
        if (!w->parent || !w->shown)
            continue;
        if (!(w->flags & HEIM_WINDOW_TOOLTIP))
            taking = true;
        heim_msg_t msg = {0};
        msg.type = HEIM_EV_CLOSE;
        client_send_window(w, &msg);
        window_damage(w);
        w->shown = false;
    }
    if (S.focus && S.focus->parent && !S.focus->shown)
        window_focus(window_root(S.focus));
    input_pointer_update();
    return taking;
}

void window_show(window_t *w)
{
    if (w->parent)
    {
        w->shown = true;
        window_place(w);
        to_top(w);
        if (!(w->flags & HEIM_WINDOW_TOOLTIP))
            window_focus(w);
        window_damage(w);
        return;
    }
    gfx_rect_t f = window_frame(w);
    int32_t cascade = idunn_px(&S.theme, 28);
    int32_t step = (int32_t)(S.placed % 8) * cascade;
    S.placed++;
    w->x = (S.width - f.w) / 2 - 3 * cascade + step;
    w->y = (S.height - f.h) / 2 - 3 * cascade + step;
    if (w->x + f.w > S.width)
        w->x = S.width - f.w;
    if (w->y + f.h > S.height)
        w->y = S.height - f.h;
    if (w->x < 0)
        w->x = 0;
    if (w->y < 0)
        w->y = 0;
    w->shown = true;
    window_raise(w);
    window_focus(w);
    window_damage(w);
}

void window_set_hover(window_t *w, part_t hover, part_t pressed)
{
    if (w->hover == hover && w->pressed == pressed)
        return;
    w->hover = hover;
    w->pressed = pressed;
    w->bar_dirty = true;
    screen_damage(gfx_rect(w->x, w->y, w->content.width, TITLE_HEIGHT));
}

void window_move(window_t *w, int32_t x, int32_t y)
{
    // keep part of the title bar on the screen
    if (y < 0)
        y = 0;
    if (y > S.height - TITLE_HEIGHT)
        y = S.height - TITLE_HEIGHT;
    if (x > S.width - 60)
        x = S.width - 60;
    if (x + w->content.width < 60)
        x = 60 - w->content.width;
    if (x == w->x && y == w->y)
        return;
    window_damage(w);
    w->x = x;
    w->y = y;
    window_damage(w);
}

void window_configure(window_t *w, int32_t width, int32_t height)
{
    heim_msg_t msg = {0};
    msg.type = HEIM_EV_CONFIGURE;
    msg.configure.width = width;
    msg.configure.height = height;
    msg.configure.state = w->maximized ? HEIM_STATE_MAXIMIZED : 0;
    client_send_window(w, &msg);
}

void window_toggle_maximize(window_t *w)
{
    if (!(w->flags & HEIM_WINDOW_RESIZABLE) || !window_decorated(w))
        return;
    window_damage(w);
    if (w->maximized)
    {
        w->maximized = false;
        w->x = w->restore.x;
        w->y = w->restore.y;
        window_configure(w, w->restore.w, w->restore.h - TITLE_HEIGHT);
    }
    else
    {
        w->restore = window_frame(w);
        w->maximized = true;
        w->x = 0;
        w->y = 0;
        window_configure(w, S.width, S.height - TITLE_HEIGHT);
    }
    w->bar_dirty = true;
    window_damage(w);
}

void window_screen_changed(void)
{
    window_close_popups();
    for (window_t *w = S.bottom; w; w = w->above)
    {
        if (w->parent)
            continue;
        if (w->maximized)
        {
            window_configure(w, S.width, S.height - TITLE_HEIGHT);
            continue;
        }
        // on the screen if it fits, else at the left; the title bar stays
        // where the pointer can reach it
        gfx_rect_t f = window_frame(w);
        if (f.x + f.w > S.width)
            w->x -= f.x + f.w - S.width;
        if (f.y + TITLE_HEIGHT > S.height)
            w->y -= f.y + TITLE_HEIGHT - S.height;
        w->x = w->x < 0 ? 0 : w->x;
        w->y = w->y < 0 ? 0 : w->y;
        w->bar_dirty = true;
    }
    screen_damage(gfx_rect(0, 0, S.width, S.height));
}

void window_minimize(window_t *w)
{
    window_close_popups();
    window_damage(w);
    w->minimized = true;
    w->hover = PART_NONE;
    w->pressed = PART_NONE;
    w->bar_dirty = true;
    if (S.focus == w)
        window_focus_top();
    input_pointer_update();
}

/**
 * Alt+Tab: the lowest window comes to the top with the focus (a minimized
 * one shows again), so pressing it again goes through all of them.
 */
void window_cycle(void)
{
    window_t *w = S.bottom;
    while (w && !w->shown)
        w = w->above;
    if (!w)
        return;
    if (w == S.top && !w->minimized)
        return;
    w->minimized = false;
    window_raise(w);
    window_focus(w);
    window_damage(w);
    input_pointer_update();
}

window_t *window_at(int32_t x, int32_t y, part_t *part, uint32_t *edges)
{
    *edges = 0;
    for (window_t *w = S.top; w; w = w->below)
    {
        if (!window_visible(w) || (w->flags & HEIM_WINDOW_TOOLTIP))
            continue;
        gfx_rect_t f = window_frame(w);
        if (w->parent)
        {
            // a popup is its card: its shadow is not part of it
            f = gfx_rect(f.x + w->margin, f.y + w->margin, f.w - 2 * w->margin,
                         f.h - 2 * w->margin);
            if (gfx_rect_contains(f, x, y))
            {
                *part = PART_CONTENT;
                return w;
            }
            continue;
        }
        if (gfx_rect_contains(f, x, y))
        {
            int32_t bx = x - f.x;
            int32_t by = y - f.y;
            if (!window_decorated(w) || by >= TITLE_HEIGHT)
            {
                *part = PART_CONTENT;
                return w;
            }
            static const part_t buttons[3] = {PART_MINIMIZE, PART_MAXIMIZE, PART_CLOSE};
            for (int i = 0; i < 3; i++)
            {
                if (has_button(w, buttons[i]) &&
                    gfx_rect_contains(button_rect(w, buttons[i]), bx, by))
                {
                    *part = buttons[i];
                    return w;
                }
            }
            *part = PART_TITLE;
            return w;
        }

        // the band around a window that can be resized
        if (!(w->flags & HEIM_WINDOW_RESIZABLE) || !window_decorated(w) || w->maximized)
            continue;
        gfx_rect_t band = gfx_rect(f.x - RESIZE_BAND, f.y - RESIZE_BAND, f.w + 2 * RESIZE_BAND,
                                   f.h + 2 * RESIZE_BAND);
        if (!gfx_rect_contains(band, x, y))
            continue;
        uint32_t e = 0;
        if (x < f.x + 12)
            e |= EDGE_LEFT;
        else if (x >= f.x + f.w - 12)
            e |= EDGE_RIGHT;
        if (y < f.y + 12)
            e |= EDGE_TOP;
        else if (y >= f.y + f.h - 12)
            e |= EDGE_BOTTOM;
        *edges = e;
        *part = PART_EDGE;
        return w;
    }
    *part = PART_NONE;
    return NULL;
}
