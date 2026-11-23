/**
 * @file floating_test.c
 * @brief host tests of idunn's menus, selects, tooltips, popovers and
 *        dialogs (popups are offscreen windows here).
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void test_floating(idunn_context_t *ctx);

static int chosen;
static idunn_widget_t *chosen_by;

static void choose(idunn_widget_t *w, void *data)
{
    chosen = (int)(intptr_t)data;
    chosen_by = w;
}

static void key(idunn_window_t *win, uint16_t code, uint32_t mods)
{
    idunn_window_key(win, code, mods, "");
}

static void click_at(idunn_window_t *win, int32_t x, int32_t y)
{
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, x, y, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, x, y, 0);
}

static idunn_menu_t *menu;

static void open_menu(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_menu_popup(menu, button);
}

static void test_menu(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *row = idunn_row(0, 2);
    idunn_box_set_align(row, IDUNN_START, IDUNN_START);
    idunn_widget_t *button = idunn_add(row, idunn_button("File", IDUNN_OUTLINE, open_menu, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    menu = idunn_menu_new();
    idunn_menu_add(menu, "file-plus", "New", "Ctrl+N", choose, (void *)1);
    int open = idunn_menu_add(menu, "folder", "Open", "Ctrl+O", choose, (void *)2);
    idunn_menu_add_separator(menu);
    int wrap = idunn_menu_add_check(menu, "Word wrap", false, choose, (void *)3);
    idunn_menu_t *sub = idunn_menu_new();
    idunn_menu_add(sub, NULL, "notes.txt", NULL, choose, (void *)10);
    idunn_menu_add(sub, NULL, "todo.txt", NULL, choose, (void *)11);
    idunn_menu_add_sub(menu, NULL, "Recent", sub);
    idunn_menu_set_disabled(menu, open, true);

    // a click opens it under the button: 4 px below, as a popup with the
    // margin for its shadow; no item is current after a click
    click_at(win, button->rect.x + 10, button->rect.y + 10);
    idunn_window_t *popup = win->popup;
    CHECK(popup != NULL);
    CHECK(popup->anchor.y == button->rect.y && popup->anchor.h == button->rect.h + 4);
    CHECK_EQ(popup->margin, 28);
    // 4 + 32 + 32 + 9 + 32 + 32 + 4 high
    CHECK_EQ(popup->buffer->height, 145 + 56);
    idunn_widget_t *view = popup->root;
    CHECK(idunn_window_focus(popup) == view);

    // Down goes to New; Down again skips the disabled Open and the separator
    key(popup, IDUNN_KEY_DOWN, 0);
    key(popup, IDUNN_KEY_DOWN, 0);
    chosen = 0;
    key(popup, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen, 3);
    CHECK(chosen_by == button);
    CHECK(idunn_menu_checked(menu, wrap));
    CHECK(win->popup == NULL);
    CHECK(popup->dead);
    idunn_reap(ctx);

    // the keyboard: Enter on the button opens it with the first item current
    idunn_window_key(win, IDUNN_KEY_TAB, 0, "");
    idunn_window_key(win, IDUNN_KEY_ENTER, 0, "\n");
    popup = win->popup;
    CHECK(popup != NULL);
    // "r" goes to Recent, Right opens the sub-menu with its first item current
    idunn_window_key(popup, 0, 0, "r");
    key(popup, IDUNN_KEY_RIGHT, 0);
    idunn_window_t *subpopup = popup->popup;
    CHECK(subpopup != NULL);
    CHECK_EQ(subpopup->side, IDUNN_SIDE_RIGHT);
    // Left closes only the sub-menu
    key(subpopup, IDUNN_KEY_LEFT, 0);
    CHECK(popup->popup == NULL && win->popup == popup);
    idunn_reap(ctx);
    key(popup, IDUNN_KEY_RIGHT, 0);
    subpopup = popup->popup;
    key(subpopup, IDUNN_KEY_DOWN, 0);
    chosen = 0;
    key(subpopup, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen, 11);
    CHECK(win->popup == NULL);
    idunn_reap(ctx);

    // the mouse: the pointer makes an item current; a click chooses it
    click_at(win, button->rect.x + 10, button->rect.y + 10);
    popup = win->popup;
    idunn_window_draw(popup);
    gfx_rect_t card = popup->root->rect;
    idunn_window_pointer(popup, IDUNN_POINTER_MOTION, card.x + 20, card.y + 4 + 16, 0);
    chosen = 0;
    click_at(popup, card.x + 20, card.y + 4 + 16);
    CHECK_EQ(chosen, 1);
    idunn_reap(ctx);

    // Escape closes it; the window's own keys work again
    click_at(win, button->rect.x + 10, button->rect.y + 10);
    key(win->popup, IDUNN_KEY_ESCAPE, 0);
    CHECK(win->popup == NULL);
    idunn_reap(ctx);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
    idunn_menu_free(menu);
}

static void test_select(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *col = idunn_column(0, 2);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    static const char *const sizes[] = {"Small", "Medium", "Large"};
    chosen = 0;
    idunn_widget_t *select = idunn_add(col, idunn_select(sizes, 3, 1, choose, (void *)7));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    CHECK_EQ(select->rect.h, 32);
    CHECK_EQ(select->rect.w, 200);

    // a click opens the choices as wide as the select, the chosen one current
    click_at(win, select->rect.x + 10, select->rect.y + 10);
    idunn_window_t *popup = win->popup;
    CHECK(popup != NULL);
    idunn_window_draw(popup);
    CHECK_EQ(popup->root->rect.w, 200);
    key(popup, IDUNN_KEY_DOWN, 0);
    key(popup, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(idunn_select_chosen(select), 2);
    CHECK_EQ(chosen, 7);
    CHECK(win->popup == NULL);
    idunn_reap(ctx);

    // a press outside (the server's HEIM_EV_CLOSE) closes it with no change
    idunn_window_key(win, IDUNN_KEY_SPACE, 0, " ");
    CHECK(win->popup != NULL);
    idunn_popup_close(win->popup);
    idunn_reap(ctx);
    CHECK_EQ(idunn_select_chosen(select), 2);

    // freeing the select while its menu is open closes the menu first
    idunn_window_key(win, IDUNN_KEY_SPACE, 0, " ");
    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

static void test_tooltip(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *row = idunn_row(4, 10);
    idunn_box_set_align(row, IDUNN_START, IDUNN_START);
    idunn_widget_t *a = idunn_add(row, idunn_icon_button("search", IDUNN_GHOST, NULL, NULL));
    idunn_widget_t *b = idunn_add(row, idunn_icon_button("star", IDUNN_GHOST, NULL, NULL));
    idunn_set_tooltip(a, "Search");
    idunn_set_tooltip(b, "Add to favourites");
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    // 600 ms under the pointer, then a tooltip above the button
    idunn_window_tick(win, 1000);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, a->rect.x + 4, a->rect.y + 4, 0);
    CHECK_EQ(idunn_window_next_wake(win), 1600);
    idunn_window_tick(win, 1599);
    CHECK(win->popup == NULL);
    idunn_window_tick(win, 1600);
    CHECK(win->popup != NULL && win->popup->tooltip);
    CHECK_EQ(win->popup->side, IDUNN_SIDE_ABOVE);
    CHECK_EQ(win->popup->buffer->height, 16 + 10);

    // to the next button: the tooltip goes and the next one comes at once
    idunn_window_tick(win, 1700);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, b->rect.x + 4, b->rect.y + 4, 0);
    CHECK(win->popup == NULL);
    idunn_window_tick(win, 1750);
    CHECK(win->popup != NULL);
    // a press takes it away
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, b->rect.x + 4, b->rect.y + 4, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, b->rect.x + 4, b->rect.y + 4, 0);
    CHECK(win->popup == NULL);
    idunn_window_tick(win, 3000);
    CHECK(win->popup == NULL);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

static void open_popover(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_widget_t *content = idunn_column(2, 0);
    idunn_add(content, idunn_label("Width", IDUNN_TEXT_LABEL));
    idunn_add(content, idunn_text_field("100%"));
    idunn_popover(button, content);
}

static void test_popover(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *row = idunn_row(0, 2);
    idunn_box_set_align(row, IDUNN_START, IDUNN_START);
    idunn_widget_t *button =
        idunn_add(row, idunn_button("Size", IDUNN_OUTLINE, open_popover, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    // a card 288 wide with 16 inside; the text field in it has the focus
    click_at(win, button->rect.x + 10, button->rect.y + 10);
    idunn_window_t *popup = win->popup;
    CHECK(popup != NULL);
    idunn_window_draw(popup);
    CHECK_EQ(popup->root->rect.w, 288);
    idunn_widget_t *field = popup->root->children[0]->children[1];
    CHECK(idunn_window_focus(popup) == field);
    idunn_window_key(popup, 0, 0, "5");
    CHECK(strcmp(idunn_text_field_text(field), "5") == 0);

    // Escape: the field does not use it, so the popover closes
    key(popup, IDUNN_KEY_ESCAPE, 0);
    CHECK(win->popup == NULL);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

static int deleted;

static void delete_it(idunn_widget_t *dialog, void *data)
{
    (void)dialog;
    (void)data;
    deleted++;
}

static void ask(idunn_widget_t *button, void *data)
{
    (void)data;
    idunn_widget_t *dialog = idunn_dialog("Delete notes.txt?", "It goes to the trash.");
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, "Delete", IDUNN_DESTRUCTIVE, delete_it, NULL);
    idunn_dialog_open(button, dialog);
}

/**
 * a dialog without a field: the main button (the last one that is not
 * destructive) has the focus, so Enter is safe.
 */
static void test_dialog_default(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 600, 400);
    idunn_widget_t *row = idunn_row(0, 2);
    idunn_widget_t *owner = idunn_add(row, idunn_button("Owner", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    idunn_widget_t *save = idunn_dialog("Save the changes?", NULL);
    idunn_dialog_add_button(save, "Do not save", IDUNN_DESTRUCTIVE, NULL, NULL);
    idunn_dialog_add_button(save, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_widget_t *save_button = idunn_dialog_add_button(save, "Save", IDUNN_PRIMARY, NULL, NULL);
    idunn_dialog_open(owner, save);
    CHECK(idunn_window_focus(win) == save_button);
    idunn_dialog_close(save);

    idunn_widget_t *del = idunn_dialog("Delete for good?", NULL);
    idunn_widget_t *cancel = idunn_dialog_add_button(del, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(del, "Delete", IDUNN_DESTRUCTIVE, NULL, NULL);
    idunn_dialog_open(owner, del);
    CHECK(idunn_window_focus(win) == cancel);
    idunn_dialog_close(del);
    idunn_window_free_widgets(win);
}

static void test_dialog(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 600, 400);
    idunn_widget_t *row = idunn_row(0, 2);
    idunn_box_set_align(row, IDUNN_START, IDUNN_START);
    idunn_widget_t *button = idunn_add(row, idunn_button("Delete", IDUNN_DESTRUCTIVE, ask, NULL));
    idunn_widget_t *other = idunn_add(row, idunn_button("Other", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    // the keyboard opens it: centred, 480 wide, Cancel has the focus
    idunn_window_key(win, IDUNN_KEY_TAB, 0, "");
    idunn_window_key(win, IDUNN_KEY_ENTER, 0, "\n");
    idunn_widget_t *dialog = win->modal;
    CHECK(dialog != NULL);
    idunn_window_draw(win);
    CHECK_EQ(dialog->rect.w, 480);
    CHECK_EQ(dialog->rect.x, 60);
    CHECK_EQ(dialog->rect.y, (400 - dialog->rect.h) / 2);
    idunn_widget_t *cancel = idunn_window_focus(win);
    CHECK(cancel != NULL && cancel != button);

    // the overlay is over the window; Tab stays in the dialog
    gfx_color_t surface = idunn_color(&ctx->theme, IDUNN_COLOR_SURFACE);
    CHECK(*gfx_pixel_at(win->buffer, 590, 390) != surface);
    key(win, IDUNN_KEY_TAB, 0);
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == cancel);

    // the window behind gets no pointer
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, other->rect.x + 4, other->rect.y + 4, 0);
    CHECK(win->hover == NULL);

    // Escape closes it; the focus goes back to the button
    key(win, IDUNN_KEY_ESCAPE, 0);
    CHECK(win->modal == NULL);
    CHECK(idunn_window_focus(win) == button);
    idunn_reap(ctx);

    // the action button closes it and then calls its function
    deleted = 0;
    idunn_window_key(win, IDUNN_KEY_ENTER, 0, "\n");
    key(win, IDUNN_KEY_TAB, 0);
    idunn_window_key(win, IDUNN_KEY_ENTER, 0, "\n");
    CHECK_EQ(deleted, 1);
    CHECK(win->modal == NULL);
    idunn_reap(ctx);
    idunn_window_draw(win);
    CHECK(*gfx_pixel_at(win->buffer, 590, 390) == surface);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

static void test_damage_parts(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 600, 400);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    idunn_widget_t *a = idunn_add(col, idunn_button("Top", IDUNN_OUTLINE, NULL, NULL));
    idunn_add(col, idunn_grow(idunn_column(0, 0), 1));
    idunn_widget_t *b = idunn_add(col, idunn_button("Bottom", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    CHECK(b->rect.y > 300);

    // two changes far apart are drawn as two parts, not all between them
    gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1];
    idunn_dirty(a);
    idunn_dirty(b);
    int count = idunn_window_draw_parts(win, parts);
    CHECK_EQ(count, 2);
    int64_t drawn = 0;
    for (int i = 0; i < count; i++)
        drawn += (int64_t)parts[i].w * parts[i].h;
    CHECK(drawn < 600 * 120);

    // near ones are joined; nothing left afterwards
    idunn_dirty(a);
    idunn_dirty(a);
    CHECK_EQ(idunn_window_draw_parts(win, parts), 1);
    CHECK_EQ(idunn_window_draw_parts(win, parts), 0);

    // more parts than a window keeps: joined, still all drawn
    for (int i = 0; i < IDUNN_DAMAGE_MAX + 4; i++)
        idunn_window_add_damage(win, gfx_rect(i * 40, (i % 2) * 300, 10, 10));
    count = idunn_window_draw_parts(win, parts);
    CHECK(count == IDUNN_DAMAGE_MAX);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

/**
 * the buffer after moved pixels must be the buffer a full drawing makes.
 */
static bool same_as_full(idunn_window_t *win)
{
    gfx_buffer_t *buf = win->buffer;
    size_t size = (size_t)buf->width * buf->height;
    uint32_t *copy = malloc(size * 4);
    if (!copy)
        return false;
    for (int32_t y = 0; y < buf->height; y++)
        memcpy(copy + (size_t)y * buf->width, gfx_pixel_at(buf, 0, y), (size_t)buf->width * 4);
    idunn_window_invalidate(win);
    idunn_window_draw(win);
    bool same = true;
    for (int32_t y = 0; y < buf->height && same; y++)
        same = memcmp(copy + (size_t)y * buf->width, gfx_pixel_at(buf, 0, y),
                      (size_t)buf->width * 4) == 0;
    free(copy);
    return same;
}

static void test_scroll_moves_pixels(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 300);
    idunn_widget_t *content = idunn_column(2, 4);
    for (int i = 0; i < 40; i++)
    {
        char text[32];
        snprintf(text, sizeof(text), "Row %d of the content", i);
        idunn_add(content, idunn_button(text, i % 3 ? IDUNN_OUTLINE : IDUNN_PRIMARY, NULL, NULL));
    }
    idunn_widget_t *view = idunn_scroll_view(content);
    idunn_window_set_root(win, view);
    idunn_window_tick(win, 1000);
    idunn_window_draw(win);

    // a jump down: only the strip that comes into view (and the bar) is drawn
    gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1];
    idunn_scroll_view_scroll_to(view, 37);
    int count = idunn_window_draw_parts(win, parts);
    int64_t drawn = 0;
    for (int i = 0; i < count - 1; i++) // the last part is the moved pixels
        drawn += (int64_t)parts[i].w * parts[i].h;
    CHECK(drawn < 400 * 60);
    CHECK(same_as_full(win));

    // and back up
    idunn_scroll_view_scroll_to(view, 5);
    idunn_window_draw(win);
    CHECK(same_as_full(win));

    // a wheel step glides over some frames; each frame is right
    idunn_window_scroll(win, 100, 100, -2);
    for (uint32_t t = 1000; t <= 1200; t += 16)
    {
        idunn_window_tick(win, t);
        idunn_window_draw(win);
        CHECK(same_as_full(win));
    }
    CHECK(idunn_scroll_view_offset(view) > 5);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

// the canvas test's model: a colour for each row of 10 pixels
static struct
{
    uint32_t rows[64];
    int first;   // the model row at the top of the canvas
    int pending; // rows to move up in the next frame
    int32_t width;
    int32_t height;
    idunn_input_t last;
    int inputs;
} cv;

static void cv_draw(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data)
{
    (void)canvas;
    (void)data;
    for (int r = 0; r * 10 < rect.h; r++)
        gfx_fill_rect(buf, gfx_rect(rect.x, rect.y + r * 10, rect.w, 10),
                      cv.rows[(cv.first + r) % 64]);
}

static bool cv_input(idunn_widget_t *canvas, const idunn_input_t *in, void *data)
{
    (void)canvas;
    (void)data;
    cv.last = *in;
    cv.inputs++;
    return true;
}

static void cv_resize(idunn_widget_t *canvas, int32_t width, int32_t height, void *data)
{
    (void)canvas;
    (void)data;
    cv.width = width;
    cv.height = height;
}

static void cv_frame(idunn_widget_t *canvas, void *data)
{
    (void)data;
    if (!cv.pending)
        return;
    cv.first += cv.pending;
    idunn_canvas_move(canvas, gfx_rect(0, 0, cv.width, cv.height), -10 * cv.pending);
    cv.pending = 0;
}

static void test_canvas(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_add(col, idunn_button("Before", IDUNN_OUTLINE, NULL, NULL));
    static const idunn_canvas_hooks_t hooks = {cv_draw, cv_input, cv_resize, cv_frame};
    for (int i = 0; i < 64; i++)
        cv.rows[i] = gfx_rgb((uint8_t)(i * 37), (uint8_t)(i * 91), (uint8_t)(i * 13));
    idunn_widget_t *canvas = idunn_grow(idunn_add(col, idunn_canvas(&hooks, NULL)), 1);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    gfx_rect_t r = idunn_widget_rect(canvas);
    CHECK_EQ(cv.width, r.w);
    CHECK_EQ(cv.height, r.h);
    CHECK(r.h > 100);

    // Tab goes to the canvas, and the keyboard stays there
    idunn_set_focus(canvas);
    key(win, IDUNN_KEY_TAB, 0);
    CHECK_EQ(cv.last.type, IDUNN_INPUT_KEY);
    CHECK_EQ(cv.last.code, IDUNN_KEY_TAB);
    CHECK(idunn_window_focus(win) == canvas);

    // the pointer in canvas coordinates
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, r.x + 7, r.y + 9, 0);
    CHECK_EQ(cv.last.type, IDUNN_INPUT_PRESS);
    CHECK_EQ(cv.last.x, 7);
    CHECK_EQ(cv.last.y, 9);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, r.x + 7, r.y + 9, 0);

    // new data: a frame moves the rows up; only the new strip is drawn
    gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1];
    cv.pending = 3;
    idunn_canvas_update(canvas);
    int count = idunn_window_draw_parts(win, parts);
    int64_t drawn = 0;
    for (int i = 0; i < count - 1; i++)
        drawn += (int64_t)parts[i].w * parts[i].h;
    CHECK(drawn <= (int64_t)r.w * 31);
    CHECK(same_as_full(win));

    // a damaged rect of the canvas is drawn
    cv.rows[(cv.first + 1) % 64] = gfx_rgb(255, 0, 0);
    idunn_canvas_damage(canvas, gfx_rect(0, 10, r.w, 10));
    idunn_window_draw(win);
    CHECK(same_as_full(win));

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

static char chosen_path[512];
static int chosen_count;

static void file_chosen(const char *path, void *data)
{
    (void)data;
    chosen_count++;
    snprintf(chosen_path, sizeof(chosen_path), "%s", path);
}

static void make_file(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

/** the file dialog: into a folder, choose a file; save, and replace */
static void test_file_dialog(idunn_context_t *ctx)
{
    char dir[] = "/tmp/idunn-files-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char sub[600];
    snprintf(sub, sizeof(sub), "%s/notes", dir);
    mkdir(sub, 0755);
    make_file(dir, "b.txt");
    make_file(sub, "inside.txt");

    idunn_window_t *win = idunn_window_offscreen(ctx, 800, 700);
    idunn_widget_t *row = idunn_row(0, 0);
    idunn_widget_t *owner = idunn_add(row, idunn_button("Owner", IDUNN_OUTLINE, NULL, NULL));
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    // open: the folder first; Enter on it goes in, Enter on a file chooses it
    chosen_count = 0;
    idunn_file_dialog(owner, IDUNN_FILE_OPEN, "Open", dir, file_chosen, NULL);
    CHECK(win->modal != NULL);
    idunn_window_draw(win);
    key(win, IDUNN_KEY_DOWN, 0); // the first item: the folder
    key(win, IDUNN_KEY_ENTER, 0);
    key(win, IDUNN_KEY_DOWN, 0); // inside.txt
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen_count, 1);
    char want[700];
    snprintf(want, sizeof(want), "%s/inside.txt", sub);
    CHECK(strcmp(chosen_path, want) == 0);
    CHECK(win->modal == NULL);
    idunn_reap(ctx);

    // save: a new name is chosen at once
    snprintf(want, sizeof(want), "%s/new.txt", dir);
    idunn_file_dialog(owner, IDUNN_FILE_SAVE, "Save as", want, file_chosen, NULL);
    idunn_window_draw(win);
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen_count, 2);
    CHECK(strcmp(chosen_path, want) == 0);
    idunn_reap(ctx);

    // save over a file: a question first, then the path
    snprintf(want, sizeof(want), "%s/b.txt", dir);
    idunn_file_dialog(owner, IDUNN_FILE_SAVE, "Save as", want, file_chosen, NULL);
    idunn_window_draw(win);
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen_count, 2);
    CHECK(win->modal != NULL); // "Replace b.txt?", with Cancel focused
    idunn_window_draw(win);
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(chosen_count, 2);
    CHECK(win->modal == NULL);
    idunn_reap(ctx);

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
    unlink(want);
    snprintf(want, sizeof(want), "%s/inside.txt", sub);
    unlink(want);
    rmdir(sub);
    rmdir(dir);
}

/**
 * after a layout only the widgets that moved are drawn again, and the
 * window must be what a full drawing makes
 */
static void test_layout_damage(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 500, 400);
    idunn_widget_t *col = idunn_column(2, 4);
    idunn_widget_t *row = idunn_add(col, idunn_row(2, 0));
    idunn_box_set_align(row, IDUNN_CENTER, IDUNN_START);
    idunn_widget_t *label = idunn_add(row, idunn_label("Short", IDUNN_TEXT_BODY));
    idunn_widget_t *after = idunn_add(row, idunn_button("After", IDUNN_OUTLINE, NULL, NULL));
    idunn_widget_t *middle = idunn_add(col, idunn_button("Middle", IDUNN_PRIMARY, NULL, NULL));
    idunn_add(col, idunn_card(2));
    idunn_widget_t *last = idunn_add(col, idunn_label("The last line", IDUNN_TEXT_SMALL));
    idunn_window_set_root(win, col);
    idunn_window_tick(win, 1000);
    idunn_window_draw(win);
    (void)after;
    (void)last;

    // a wider text moves the button after it: a small part is drawn
    idunn_label_set_text(label, "A much longer text than before");
    gfx_rect_t parts[IDUNN_DAMAGE_MAX + 1];
    int count = idunn_window_draw_parts(win, parts);
    int64_t drawn = 0;
    for (int i = 0; i < count; i++)
        drawn += (int64_t)parts[i].w * parts[i].h;
    CHECK(drawn < 500 * 400 / 3);
    CHECK(same_as_full(win));

    // hidden: what was under it moves up
    idunn_set_hidden(middle, true);
    idunn_window_draw(win);
    CHECK(same_as_full(win));
    // shown again, at the same place as before
    idunn_set_hidden(middle, false);
    idunn_window_draw(win);
    CHECK(same_as_full(win));

    // a new widget and a freed one
    idunn_widget_t *extra = idunn_add(row, idunn_button("New", IDUNN_GHOST, NULL, NULL));
    idunn_window_draw(win);
    CHECK(same_as_full(win));
    idunn_widget_free(extra);
    idunn_window_draw(win);
    CHECK(same_as_full(win));

    idunn_window_free_widgets(win);
    idunn_reap(ctx);
    free(win);
}

void test_floating(idunn_context_t *ctx)
{
    test_canvas(ctx);
    test_damage_parts(ctx);
    test_scroll_moves_pixels(ctx);
    test_menu(ctx);
    test_select(ctx);
    test_tooltip(ctx);
    test_popover(ctx);
    test_dialog(ctx);
    test_dialog_default(ctx);
    test_file_dialog(ctx);
    test_layout_damage(ctx);
}
