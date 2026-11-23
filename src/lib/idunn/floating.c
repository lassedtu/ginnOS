/**
 * @file floating.c
 * @brief idunn: what floats over a window: tooltips (the primary colour,
 *        small text), popovers (a card next to a widget) and dialogs (a
 *        card in the middle of the window over the overlay colour, with
 *        buttons at the bottom right; the window behind gets no input).
 */

#include "idunn_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TIP_PAD_X     10
#define TIP_PAD_Y     5
#define TIP_GAP       6   // between the tooltip and its widget
#define POPOVER_WIDTH 288
#define POPOVER_GAP   4
#define MARGIN        28  // room for the md shadow of a popover

typedef struct
{
    char *text;
} tip_t;

static void tip_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    (void)max_width;
    const tip_t *tip = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    bragi_face_t *face = idunn_face(ctx, IDUNN_TEXT_SMALL);
    int32_t length = (int32_t)strlen(tip->text);
    *width = 2 * idunn_px(t, TIP_PAD_X) + (face ? bragi_caret_x(face, tip->text, length, length) : 0);
    *height = 2 * idunn_px(t, TIP_PAD_Y) + idunn_text_style(t, IDUNN_TEXT_SMALL).line_height;
}

static void tip_draw(idunn_widget_t *w, gfx_buffer_t *buf)
{
    const tip_t *tip = w->data;
    idunn_context_t *ctx = idunn_ctx(w);
    const idunn_theme_t *t = &ctx->theme;
    gfx_fill_round_rect(buf, w->rect, idunn_radius_sm(t), idunn_color(t, IDUNN_COLOR_PRIMARY));
    idunn_draw_text(ctx, buf, IDUNN_TEXT_SMALL, w->rect.x + idunn_px(t, TIP_PAD_X),
                    w->rect.y + idunn_px(t, TIP_PAD_Y), 0, tip->text,
                    idunn_color(t, IDUNN_COLOR_PRIMARY_FOREGROUND));
}

static void tip_free(idunn_widget_t *w)
{
    tip_t *tip = w->data;
    free(tip->text);
    free(tip);
}

static const idunn_class_t tip_class = {
    .name = "tooltip",
    .measure = tip_measure,
    .draw = tip_draw,
    .free = tip_free,
};

void idunn_tooltip_show(idunn_widget_t *w)
{
    tip_t *tip = calloc(1, sizeof(*tip));
    if (!tip || !(tip->text = strdup(w->tooltip)))
    {
        free(tip);
        return;
    }
    idunn_widget_t *view = idunn_widget_new(&tip_class, tip, 0);
    if (!view)
    {
        tip_free(&(idunn_widget_t){.data = tip});
        return;
    }
    // over the widget, in the middle, a little apart
    int32_t gap = idunn_px(idunn_theme(w), TIP_GAP);
    gfx_rect_t anchor = gfx_rect(w->rect.x, w->rect.y - gap, w->rect.w, w->rect.h + gap);
    idunn_popup_open(w, view, anchor, IDUNN_SIDE_ABOVE, 0, true);
}

void idunn_popover(idunn_widget_t *owner, idunn_widget_t *content)
{
    if (!owner || !owner->window)
    {
        idunn_widget_free(content);
        return;
    }
    const idunn_theme_t *t = idunn_theme(owner);
    idunn_widget_t *card = idunn_card_box(3, 4, IDUNN_SHADOW_MD);
    if (!card)
    {
        idunn_widget_free(content);
        return;
    }
    idunn_set_size(card, POPOVER_WIDTH, 0);
    idunn_add(card, content);
    gfx_rect_t anchor = owner->rect;
    anchor.h += idunn_px(t, POPOVER_GAP);
    idunn_window_t *popup =
        idunn_popup_open(owner, card, anchor, IDUNN_SIDE_BELOW, idunn_px(t, MARGIN), false);
    if (popup)
        idunn_focus_first(card);
}

void idunn_popover_close(idunn_widget_t *inside)
{
    if (inside && inside->window && inside->window->opener)
        idunn_popup_close(inside->window);
}

// dialogs

typedef struct
{
    idunn_widget_t *footer;
    struct
    {
        idunn_callback_t call;
        void *data;
        idunn_widget_t *button;
        bool destructive;
    } buttons[4];
    int count;
} dialog_t;

static void dialog_measure(idunn_widget_t *w, int32_t max_width, int32_t *width, int32_t *height)
{
    *width = 0;
    *height = 0;
    if (w->child_count)
        idunn_measure(w->children[0], max_width, width, height);
}

static void dialog_layout(idunn_widget_t *w)
{
    if (w->child_count)
        idunn_place(w->children[0], w->rect);
}

static void dialog_free(idunn_widget_t *w)
{
    free(w->data);
}

static const idunn_class_t dialog_class = {
    .name = "dialog",
    .measure = dialog_measure,
    .layout = dialog_layout,
    .free = dialog_free,
};

static idunn_widget_t *dialog_of(idunn_widget_t *w)
{
    while (w && w->cls != &dialog_class)
        w = w->parent;
    return w;
}

idunn_widget_t *idunn_dialog(const char *title, const char *description)
{
    dialog_t *d = calloc(1, sizeof(*d));
    idunn_widget_t *w = d ? idunn_widget_new(&dialog_class, d, 0) : NULL;
    idunn_widget_t *card = w ? idunn_add(w, idunn_card_box(4, 6, IDUNN_SHADOW_WINDOW)) : NULL;
    if (!card)
    {
        if (w)
            idunn_widget_free(w);
        else
            free(d);
        return NULL;
    }
    idunn_card_header(card, title, description);
    return w;
}

idunn_widget_t *idunn_dialog_content(idunn_widget_t *dialog)
{
    return dialog && dialog->cls == &dialog_class ? dialog->children[0] : NULL;
}

static void button_clicked(idunn_widget_t *button, void *data)
{
    idunn_widget_t *w = dialog_of(button);
    if (!w)
        return;
    dialog_t *d = w->data;
    int i = (int)(intptr_t)data;
    idunn_callback_t call = d->buttons[i].call;
    void *call_data = d->buttons[i].data;
    idunn_dialog_close(w);
    if (call)
        call(w, call_data);
}

idunn_widget_t *idunn_dialog_add_button(idunn_widget_t *dialog, const char *text,
                                        idunn_variant_t variant, idunn_callback_t on_click,
                                        void *data)
{
    if (!dialog || dialog->cls != &dialog_class)
        return NULL;
    dialog_t *d = dialog->data;
    if (d->count == (int)(sizeof(d->buttons) / sizeof(d->buttons[0])))
        return NULL;
    if (!d->footer)
    {
        d->footer = idunn_add(dialog->children[0], idunn_row(2, 0));
        idunn_box_set_align(d->footer, IDUNN_CENTER, IDUNN_END);
    }
    d->buttons[d->count].call = on_click;
    d->buttons[d->count].data = data;
    d->buttons[d->count].destructive = variant == IDUNN_DESTRUCTIVE;
    idunn_widget_t *b = idunn_add(d->footer, idunn_button(text, variant, button_clicked,
                                                          (void *)(intptr_t)d->count));
    d->buttons[d->count++].button = b;
    return b;
}

void idunn_dialog_open(idunn_widget_t *owner, idunn_widget_t *dialog)
{
    idunn_window_t *win = owner ? owner->window : NULL;
    if (!win || !dialog || dialog->cls != &dialog_class || win->modal)
    {
        idunn_widget_free(dialog);
        return;
    }
    if (win->popup)
        idunn_popup_close(win->popup);
    win->modal_return = win->focus;
    win->modal = dialog;
    dialog->parent = NULL;
    idunn_set_window(dialog, win);
    win->needs_layout = true;
    idunn_window_invalidate(win);
    // the focus goes inside: the first field, or else the main button (the
    // last one that is not destructive), so Enter does what is safe
    dialog_t *d = dialog->data;
    idunn_widget_t *card = dialog->children[0];
    bool field = false;
    for (int i = 0; i < card->child_count && !field; i++)
    {
        if (card->children[i] != d->footer)
        {
            idunn_widget_t *before = win->focus;
            idunn_focus_first(card->children[i]);
            field = win->focus != before;
        }
    }
    if (!field && d->footer)
    {
        idunn_widget_t *main_button = NULL;
        for (int i = 0; i < d->count; i++)
            if (!d->buttons[i].destructive)
                main_button = d->buttons[i].button;
        if (main_button)
            idunn_set_focus(main_button);
        else
            idunn_focus_first(d->footer);
    }
}

void idunn_dialog_close(idunn_widget_t *dialog)
{
    idunn_window_t *win = dialog ? dialog->window : NULL;
    if (!win || win->modal != dialog)
        return;
    win->modal = NULL;
    idunn_widget_t *back = win->modal_return;
    win->modal_return = NULL;
    // the focus and the pointer are in the dialog: they leave it
    win->focus = NULL;
    win->hover = NULL;
    win->pressed = NULL;
    idunn_free_later(dialog);
    win->needs_layout = true;
    idunn_window_invalidate(win);
    if (back)
        idunn_focus(back);
}

void idunn_dialog_dismiss(idunn_window_t *win)
{
    if (win->modal)
        idunn_dialog_close(win->modal);
}
