/**
 * @file popup.c
 * @brief idunn: popup windows (menus, a select's choices, popovers,
 *        tooltips). in an app they are heimdall popups, which the server
 *        puts next to a rect of their window and closes on a press
 *        outside them; in host tests they are offscreen windows.
 */

#include "idunn_internal.h"

#include <stdlib.h>

static idunn_window_t *offscreen_new(idunn_window_t *win, int32_t width, int32_t height,
                                     bool tooltip)
{
    (void)tooltip;
    idunn_window_t *popup = idunn_window_offscreen(win->ctx, width, height);
    if (popup)
        popup->now = win->now;
    return popup;
}

static void offscreen_free(idunn_window_t *popup)
{
    idunn_window_free_widgets(popup);
    free(popup);
}

idunn_window_t *idunn_popup_open(idunn_widget_t *owner, idunn_widget_t *root, gfx_rect_t anchor,
                                 idunn_side_t side, int32_t margin, bool tooltip)
{
    idunn_window_t *win = owner ? owner->window : NULL;
    if (!win || !root)
    {
        idunn_widget_free(root);
        return NULL;
    }
    if (win->popup)
        idunn_popup_close(win->popup);

    int32_t width;
    int32_t height;
    idunn_set_window(root, win); // measuring needs the theme
    idunn_measure(root, 0, &width, &height);
    idunn_window_t *(*make)(idunn_window_t *, int32_t, int32_t, bool) =
        win->popup_new ? win->popup_new : offscreen_new;
    idunn_window_t *popup = make(win, width + 2 * margin, height + 2 * margin, tooltip);
    if (!popup)
    {
        idunn_widget_free(root);
        return NULL;
    }
    popup->opener = win;
    popup->transparent = true;
    popup->margin = margin;
    popup->anchor = anchor;
    popup->side = side;
    popup->tooltip = tooltip;
    popup->popup_new = win->popup_new;
    popup->popup_place = win->popup_place;
    popup->popup_free = win->popup_free;
    popup->clipboard_set = win->clipboard_set;
    popup->clipboard_get = win->clipboard_get;
    popup->now = win->now;
    win->popup = popup;
    idunn_window_set_root(popup, root);
    if (popup->popup_place)
        popup->popup_place(popup);
    return popup;
}

void idunn_popup_close(idunn_window_t *popup)
{
    if (!popup || popup->dead)
        return;
    if (popup->popup)
        idunn_popup_close(popup->popup);
    if (popup->closed)
        popup->closed(popup);
    if (popup->opener && popup->opener->popup == popup)
        popup->opener->popup = NULL;
    popup->opener = NULL;
    popup->dead = true;
    popup->next_dead = popup->ctx->dead;
    popup->ctx->dead = popup;
}

void idunn_reap(idunn_context_t *ctx)
{
    for (int i = 0; i < ctx->dead_count; i++)
        idunn_widget_free(ctx->dead_widgets[i]);
    ctx->dead_count = 0;
    while (ctx->dead)
    {
        idunn_window_t *popup = ctx->dead;
        ctx->dead = popup->next_dead;
        void (*release)(idunn_window_t *) = popup->popup_free ? popup->popup_free : offscreen_free;
        release(popup);
    }
}

void idunn_popup_close_all(idunn_window_t *popup)
{
    while (popup && popup->opener && popup->opener->opener)
        popup = popup->opener;
    idunn_popup_close(popup);
}
