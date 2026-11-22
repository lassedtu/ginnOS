/**
 * @file floating_test.c
 * @brief host tests of idunn's menus, selects, tooltips, popovers and
 *        dialogs (popups are offscreen windows here).
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    gfx_rect_t parts[IDUNN_DAMAGE_MAX];
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

void test_floating(idunn_context_t *ctx)
{
    test_damage_parts(ctx);
    test_menu(ctx);
    test_select(ctx);
    test_tooltip(ctx);
    test_popover(ctx);
    test_dialog(ctx);
}
