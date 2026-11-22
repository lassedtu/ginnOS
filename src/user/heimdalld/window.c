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

// the shadow picture: a window of TPL_W x TPL_H at (SHADOW_PAD, SHADOW_PAD).
// TPL_H is tall enough that one row is straight for the window's rim and
// for the shadow below it (the shadow is SHADOW_DROP lower)
#define TPL_W (2 * CORNER_RADIUS + 2)
#define TPL_H (2 * CORNER_RADIUS + SHADOW_DROP + 2)
#define TPL_STRETCH_X (SHADOW_PAD + CORNER_RADIUS + 1)
#define TPL_STRETCH_Y (SHADOW_PAD + CORNER_RADIUS + SHADOW_DROP + 1)

static gfx_buffer_t *shadow;
static uint8_t corner[CORNER_RADIUS * CORNER_RADIUS]; // the bottom left corner's coverage

static const gfx_color_t bar_focused = 0xFFE6E8EEu;
static const gfx_color_t bar_unfocused = 0xFFF4F4F6u;

void window_init(void)
{
    // the shadow: rings of faint black, and a thin rim around the window
    int32_t w = TPL_W + 2 * SHADOW_PAD;
    int32_t h = TPL_H + 2 * SHADOW_PAD + SHADOW_DROP;
    shadow = gfx_buffer_new(w, h);
    if (shadow)
    {
        for (int32_t i = 12; i > 0; i--)
            gfx_fill_round_rect(shadow,
                                gfx_rect(SHADOW_PAD - i, SHADOW_PAD - i + SHADOW_DROP,
                                         TPL_W + 2 * i, TPL_H + 2 * i),
                                CORNER_RADIUS + i, gfx_rgba(0, 0, 0, 6));
        gfx_fill_round_rect(shadow,
                            gfx_rect(SHADOW_PAD - 1, SHADOW_PAD - 1, TPL_W + 2, TPL_H + 2),
                            CORNER_RADIUS + 1, gfx_rgba(0, 0, 0, 46));
    }

    // the coverage of a round corner
    gfx_buffer_t *disc = gfx_buffer_new(2 * CORNER_RADIUS, 2 * CORNER_RADIUS);
    if (disc)
    {
        gfx_fill_round_rect(disc, gfx_rect(0, 0, 2 * CORNER_RADIUS, 2 * CORNER_RADIUS),
                            CORNER_RADIUS, gfx_rgb(255, 255, 255));
        for (int32_t y = 0; y < CORNER_RADIUS; y++)
            for (int32_t x = 0; x < CORNER_RADIUS; x++)
                corner[y * CORNER_RADIUS + x] =
                    gfx_alpha(*gfx_pixel_at(disc, x, CORNER_RADIUS + y));
        gfx_buffer_free(disc);
    }
}

/* ---- geometry ------------------------------------------------------------- */

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
    int32_t pad = SHADOW_PAD > RESIZE_BAND ? SHADOW_PAD : RESIZE_BAND;
    return gfx_rect(f.x - pad, f.y - pad, f.w + 2 * pad, f.h + 2 * pad + SHADOW_DROP);
}

bool window_visible(const window_t *w)
{
    return w->shown && !w->minimized && w->content.pixels;
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

/* ---- the title bar -------------------------------------------------------- */

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
    int32_t x = w->content.width - 6 - (slot + 1) * BUTTON_WIDTH;
    return gfx_rect(x, (TITLE_HEIGHT - BUTTON_HEIGHT) / 2, BUTTON_WIDTH, BUTTON_HEIGHT);
}

static bool has_button(const window_t *w, part_t part)
{
    return part != PART_MAXIMIZE || (w->flags & HEIM_WINDOW_RESIZABLE);
}

static void draw_button(window_t *w, part_t part, const icon_t *icon, bool focused)
{
    gfx_rect_t r = button_rect(w, part);
    gfx_color_t ink = focused ? gfx_rgb(60, 64, 76) : gfx_rgb(150, 152, 160);
    if (w->hover == part || w->pressed == part)
    {
        bool close = part == PART_CLOSE;
        gfx_color_t back = close ? gfx_rgb(226, 72, 72) : gfx_rgba(0, 0, 0, 22);
        if (w->pressed == part)
            back = close ? gfx_rgb(190, 50, 50) : gfx_rgba(0, 0, 0, 44);
        gfx_fill_round_rect(w->bar, r, 6, back);
        if (close)
            ink = gfx_rgb(255, 255, 255);
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
    bool focused = S.focus == w;
    gfx_clear(w->bar, GFX_TRANSPARENT);
    gfx_fill_round_rect(w->bar, gfx_rect(0, 0, width, TITLE_HEIGHT + CORNER_RADIUS),
                        rounded(w) ? CORNER_RADIUS : 0, focused ? bar_focused : bar_unfocused);
    gfx_fill_rect(w->bar, gfx_rect(0, TITLE_HEIGHT - 1, width, 1), gfx_rgba(0, 0, 0, 40));

    // the title, in the middle, between the buttons
    int32_t buttons = 6 + 3 * BUTTON_WIDTH + 8;
    if (S.title_face && width > 2 * buttons)
    {
        bragi_face_t *f = S.title_face;
        int32_t lh = bragi_line_height(f);
        gfx_rect_t box = gfx_rect(buttons, (TITLE_HEIGHT - lh) / 2 + 1, width - 2 * buttons, lh);
        bragi_draw_box(w->bar, f, box, w->title, BRAGI_ALIGN_CENTER,
                       focused ? gfx_rgb(40, 42, 50) : gfx_rgb(140, 142, 150));
    }

    draw_button(w, PART_MINIMIZE, S.icon_minimize, focused);
    if (has_button(w, PART_MAXIMIZE))
        draw_button(w, PART_MAXIMIZE, w->maximized ? S.icon_restore : S.icon_maximize, focused);
    draw_button(w, PART_CLOSE, S.icon_close, focused);
    w->bar_dirty = false;
}

/* ---- drawing -------------------------------------------------------------- */

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
    const int32_t left = TPL_STRETCH_X;                  // columns before the stretched one
    const int32_t right = shadow->width - left - 1;      // columns after it
    const int32_t top = TPL_STRETCH_Y;
    const int32_t bottom = shadow->height - top - 1;
    int32_t x0 = f.x - SHADOW_PAD;
    int32_t y0 = f.y - SHADOW_PAD;
    int32_t x1 = f.x + f.w + SHADOW_PAD; // the right end
    int32_t y1 = f.y + f.h + SHADOW_PAD + SHADOW_DROP;
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
    int32_t r = rounded(w) && c.w >= 2 * CORNER_RADIUS && c.h >= CORNER_RADIUS ? CORNER_RADIUS
                                                                                : 0;
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

/* ---- the window list ------------------------------------------------------ */

window_t *window_new(client_t *client, uint32_t id, uint32_t flags, const char *title)
{
    window_t *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->client = client;
    w->id = id;
    w->flags = flags & (HEIM_WINDOW_RESIZABLE | HEIM_WINDOW_ALPHA | HEIM_WINDOW_UNDECORATED);
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
    if (had_focus)
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

/* ---- states --------------------------------------------------------------- */

void window_raise(window_t *w)
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
        S.focus->bar_dirty = true;
        window_damage(S.focus);
        send_focus(S.focus, false);
    }
    S.focus = w;
    if (w)
    {
        w->bar_dirty = true;
        window_damage(w);
        send_focus(w, true);
    }
}

void window_focus_top(void)
{
    for (window_t *w = S.top; w; w = w->below)
    {
        if (window_visible(w))
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
void window_show(window_t *w)
{
    gfx_rect_t f = window_frame(w);
    int32_t step = (int32_t)(S.placed % 8) * 28;
    S.placed++;
    w->x = (S.width - f.w) / 2 - 3 * 28 + step;
    w->y = (S.height - f.h) / 2 - 3 * 28 + step;
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

void window_minimize(window_t *w)
{
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

/* ---- hit testing ---------------------------------------------------------- */

window_t *window_at(int32_t x, int32_t y, part_t *part, uint32_t *edges)
{
    *edges = 0;
    for (window_t *w = S.top; w; w = w->below)
    {
        if (!window_visible(w))
            continue;
        gfx_rect_t f = window_frame(w);
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
