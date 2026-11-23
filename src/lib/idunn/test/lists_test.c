/**
 * @file lists_test.c
 * @brief host tests of idunn's structure: scroll views, cards, tabs and
 *        lists.
 */

#include "../idunn_internal.h"
#include "hosttest.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void test_lists(idunn_context_t *ctx);

static void key(idunn_window_t *win, uint16_t code, uint32_t mods)
{
    idunn_window_key(win, code, mods, "");
}

static void test_scroll(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *root = idunn_column(0, 0);
    idunn_widget_t *content = idunn_column(2, 4);
    idunn_widget_t *buttons[20];
    for (int i = 0; i < 20; i++)
        buttons[i] = idunn_add(content, idunn_button("Row", IDUNN_SECONDARY, NULL, NULL));
    idunn_widget_t *view = idunn_grow(idunn_add(root, idunn_scroll_view(content)), 1);
    idunn_window_set_root(win, root);
    idunn_window_draw(win);

    // the view takes the window's height; the content is 16 + 20 * 32 + 19 * 8 tall
    CHECK_EQ(view->rect.h, 200);
    CHECK_EQ(content->rect.h, 32 + 20 * 32 + 19 * 8);

    // the wheel: 48 a step, and at the top the event goes on to the parents
    idunn_window_scroll(win, 100, 100, -1);
    CHECK_EQ(idunn_scroll_view_offset(view), 48);
    idunn_window_tick(win, 1000);
    idunn_window_draw(win);
    CHECK_EQ(content->rect.y, -48);
    idunn_window_scroll(win, 100, 100, 5);
    CHECK_EQ(idunn_scroll_view_offset(view), 0);
    idunn_window_scroll(win, 100, 100, -100);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);

    // a hidden widget below the view is not hit, nor drawn over the window
    idunn_window_tick(win, 2000);
    idunn_window_draw(win);
    idunn_scroll_view_scroll_to(view, 0);
    idunn_window_draw(win);
    CHECK(buttons[10]->rect.y > 200);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 50, buttons[0]->rect.y + 4, 0);
    CHECK(win->hover == buttons[0]);

    // Tab to a button out of view scrolls it in, with room for the ring
    for (int i = 0; i < 8; i++)
        key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == buttons[7]);
    idunn_window_tick(win, 3000);
    idunn_window_draw(win);
    CHECK(buttons[7]->rect.y + buttons[7]->rect.h <= 200 - 3);
    CHECK(buttons[7]->rect.y >= 0);

    // keys a button does not use scroll the view: Home, End, Page Down
    key(win, IDUNN_KEY_HOME, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), 0);
    key(win, IDUNN_KEY_END, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);
    key(win, IDUNN_KEY_HOME, 0);
    key(win, IDUNN_KEY_PAGEDOWN, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), 200 - 48);

    // the bar: dragging the thumb to the bottom scrolls to the end
    idunn_window_tick(win, 4000);
    idunn_scroll_view_scroll_to(view, 0);
    idunn_window_draw(win);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 296, 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, 296, 10, 0);
    idunn_window_pointer(win, IDUNN_POINTER_MOTION, 296, 400, 0);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, 296, 400, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), content->rect.h - 200);

    // the thumb is drawn at the right edge in the border colour, mixed
    idunn_window_draw(win);
    gfx_color_t surface = idunn_color(&ctx->theme, IDUNN_COLOR_SURFACE);
    CHECK(*gfx_pixel_at(win->buffer, 296, 196) != surface);
    CHECK(*gfx_pixel_at(win->buffer, 296, 10) == surface);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_hidden(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(2, 0);
    idunn_box_set_align(col, IDUNN_START, IDUNN_START);
    idunn_widget_t *a = idunn_add(col, idunn_button("A", IDUNN_SECONDARY, NULL, NULL));
    idunn_widget_t *b = idunn_add(col, idunn_button("B", IDUNN_SECONDARY, NULL, NULL));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 40);

    // a hidden widget takes no room and no Tab
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == a);
    idunn_set_hidden(a, true);
    CHECK(idunn_window_focus(win) == NULL);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 0);
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == b);
    idunn_set_hidden(a, false);
    idunn_window_draw(win);
    CHECK_EQ(b->rect.y, 40);

    // a card: the card colour inside a border, 24 px in
    idunn_widget_t *card = idunn_add(col, idunn_card(4));
    idunn_card_header(card, "Storage", "How the disk is used.");
    idunn_window_draw(win);
    CHECK_EQ(card->children[0]->rect.x, card->rect.x + 24);
    CHECK(*gfx_pixel_at(win->buffer, card->rect.x + card->rect.w / 2, card->rect.y + 2) ==
          idunn_color(&ctx->theme, IDUNN_COLOR_CARD));

    idunn_window_free_widgets(win);
    free(win);
}

static int changes;
static int activations;

static void count_change(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    changes++;
}

static void count_activate(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    activations++;
}

static void click_at(idunn_window_t *win, int32_t x, int32_t y, uint32_t mods)
{
    idunn_window_pointer(win, IDUNN_POINTER_PRESS, x, y, mods);
    idunn_window_pointer(win, IDUNN_POINTER_RELEASE, x, y, mods);
}

static void test_tabs(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 400, 200);
    idunn_widget_t *col = idunn_column(0, 0);
    changes = 0;
    idunn_widget_t *tabs = idunn_add(col, idunn_tabs(count_change, NULL));
    idunn_widget_t *one = idunn_tabs_add(tabs, "Account", idunn_button("A", IDUNN_SECONDARY, NULL, NULL));
    idunn_widget_t *two = idunn_tabs_add(tabs, "Password", idunn_button("B", IDUNN_SECONDARY, NULL, NULL));
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // the strip is 30 high, the page 16 below it; only the active page shows
    CHECK_EQ(one->rect.y, 30 + 16);
    CHECK(!(one->state & IDUNN_HIDDEN));
    CHECK(two->state & IDUNN_HIDDEN);

    // a press on the second name; the arrows go round
    gfx_rect_t strip = tabs->rect;
    click_at(win, strip.x + 3 + 70 + 20, strip.y + 15, 0);
    CHECK_EQ(idunn_tabs_active(tabs), 1);
    CHECK(one->state & IDUNN_HIDDEN);
    idunn_window_draw(win);
    CHECK_EQ(two->rect.y, 46);
    key(win, IDUNN_KEY_RIGHT, 0);
    CHECK_EQ(idunn_tabs_active(tabs), 0);
    CHECK_EQ(changes, 2);

    // one Tab stop for the strip, then the page
    idunn_window_draw(win);
    key(win, IDUNN_KEY_TAB, 0);
    CHECK(idunn_window_focus(win) == one);
    key(win, IDUNN_KEY_TAB, IDUNN_MOD_SHIFT);
    CHECK(idunn_window_focus(win) == tabs);

    idunn_window_free_widgets(win);
    free(win);
}

/**
 * a row that keeps its end: the last children are in view, the first are
 * out of it (left of the row) and the row clips them.
 */
static void test_keep_end(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 200, 100);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *bar = idunn_add(col, idunn_row(0, 0));
    idunn_widget_t *parts[6];
    for (int i = 0; i < 6; i++)
    {
        parts[i] = idunn_add(bar, idunn_button("Folder", IDUNN_GHOST, NULL, NULL));
        idunn_set_size(parts[i], 60, 0);
    }
    idunn_box_set_keep_end(bar, true);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    // 6 of 60 in 200: the last three fit, the third is the first in view
    CHECK_EQ(parts[5]->rect.x + parts[5]->rect.w, 180);
    CHECK_EQ(parts[3]->rect.x, 0);
    CHECK(parts[2]->rect.x < 0);
    CHECK(bar->flags & IDUNN_CLIPS);
    idunn_window_free_widgets(win);
}

static int menus;
static void count_menu(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    menus++;
}

static int window_keys;
static bool window_key(idunn_window_t *win, uint16_t code, uint32_t mods, const char *text,
                       void *data)
{
    (void)win;
    (void)mods;
    (void)text;
    (void)data;
    window_keys++;
    return code == IDUNN_KEY_BACKSPACE;
}

/**
 * a right press: the list selects the item under it and calls on_menu;
 * beside the items nothing stays selected. keys the list does not use go
 * to the window's hook.
 */
static int middles;
static idunn_widget_t *middle_on;

static void count_middle(idunn_widget_t *w, void *data)
{
    (void)data;
    middles++;
    middle_on = w;
}

static char edited[64];
static int edits;
static int edited_item;

static void edit_done(idunn_widget_t *list, int item, const char *text, void *data)
{
    (void)list;
    (void)data;
    edits++;
    edited_item = item;
    snprintf(edited, sizeof(edited), "%s", text ? text : "(none)");
}

/** a rename in place: Enter keeps, Escape takes back, a press elsewhere keeps */
static void test_edit_in_place(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 400);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *list = idunn_grow(idunn_add(col, idunn_list_view()), 1);
    idunn_list_view_add(list, "file", "notes.txt", NULL);
    idunn_list_view_add(list, "file", "photo.png", NULL);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);
    uint32_t now = 1000;
    idunn_window_tick(win, now);

    // the name without its extension is selected: typing replaces it
    edits = 0;
    CHECK(idunn_list_view_edit(list, 0, 0, 5, edit_done, NULL));
    CHECK(idunn_list_view_editing(list));
    idunn_window_draw(win);
    idunn_window_key(win, IDUNN_KEY_A, 0, "a");
    idunn_window_key(win, IDUNN_KEY_A, 0, "b");
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(edits, 0); // not in the event: on the timer
    idunn_window_tick(win, now += 10);
    CHECK_EQ(edits, 1);
    CHECK(strcmp(edited, "ab.txt") == 0);
    CHECK(!idunn_list_view_editing(list));
    CHECK(idunn_window_focus(win) == list);

    // Escape takes it back
    CHECK(idunn_list_view_edit(list, 1, 0, (size_t)-1, edit_done, NULL));
    idunn_window_key(win, IDUNN_KEY_X, 0, "x");
    key(win, IDUNN_KEY_ESCAPE, 0);
    idunn_window_tick(win, now += 10);
    CHECK_EQ(edits, 2);
    CHECK_EQ(edited_item, 1);
    CHECK(strcmp(edited, "(none)") == 0);

    // a press on another item keeps the text
    CHECK(idunn_list_view_edit(list, 1, 0, (size_t)-1, edit_done, NULL));
    idunn_window_draw(win);
    idunn_window_key(win, IDUNN_KEY_A, 0, "y");
    click_at(win, 50, 4 + 10, 0);
    idunn_window_tick(win, now += 10);
    CHECK_EQ(edits, 3);
    CHECK(strcmp(edited, "y") == 0);

    // clearing the list drops the field, with no result
    CHECK(idunn_list_view_edit(list, 0, 0, 0, edit_done, NULL));
    idunn_list_view_clear(list);
    idunn_window_tick(win, now += 10);
    CHECK_EQ(edits, 3);
    CHECK(!idunn_list_view_editing(list));
    idunn_window_free_widgets(win);
}

/** a single-click list (a launcher): a click activates, the keys only move */
static void test_single_click(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 400);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *list = idunn_grow(idunn_add(col, idunn_list_view()), 1);
    for (int i = 0; i < 3; i++)
        idunn_list_view_add(list, "file", "name", NULL);
    idunn_list_view_set_single_click(list, true);
    idunn_list_view_on_activate(list, count_activate, NULL);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    activations = 0;
    click_at(win, 50, 4 + 33 + 10, 0);
    CHECK_EQ(activations, 1);
    CHECK_EQ(idunn_list_view_current(list), 1);
    key(win, IDUNN_KEY_DOWN, 0);
    CHECK_EQ(activations, 1);
    CHECK_EQ(idunn_list_view_current(list), 2);
    key(win, IDUNN_KEY_ENTER, 0);
    CHECK_EQ(activations, 2);
    idunn_window_free_widgets(win);
}

/** a middle click goes to the nearest widget with a hook (a task button) */
static void test_middle_click(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 100);
    idunn_widget_t *row = idunn_row(0, 0);
    idunn_box_set_align(row, IDUNN_START, IDUNN_START);
    idunn_widget_t *a = idunn_add(row, idunn_button("A", IDUNN_GHOST, NULL, NULL));
    idunn_widget_t *b = idunn_add(row, idunn_button("B", IDUNN_GHOST, NULL, NULL));
    idunn_button_set_picture(b, "text-editor");
    idunn_button_set_active(b, true);
    idunn_button_set_dim(a, true);
    idunn_on_middle_click(b, count_middle, NULL);
    idunn_on_middle_click(row, count_middle, NULL);
    idunn_window_set_root(win, row);
    idunn_window_draw(win);

    middles = 0;
    idunn_window_pointer(win, IDUNN_POINTER_MIDDLE, b->rect.x + 4, b->rect.y + 4, 0);
    CHECK_EQ(middles, 1);
    CHECK(middle_on == b);
    // on a, which has none: the row's
    idunn_window_pointer(win, IDUNN_POINTER_MIDDLE, a->rect.x + 4, a->rect.y + 4, 0);
    CHECK_EQ(middles, 2);
    CHECK(middle_on == row);
    idunn_window_free_widgets(win);
}

static void test_list_menu(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 400);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *list = idunn_list_view();
    for (int i = 0; i < 5; i++)
        idunn_list_view_add(list, "file", "name", NULL);
    idunn_list_view_set_multiple(list, true);
    idunn_grow(idunn_add(col, list), 1);
    idunn_list_view_on_menu(list, count_menu, NULL);
    idunn_window_on_key(win, window_key, NULL);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    menus = 0;
    idunn_window_pointer(win, IDUNN_POINTER_MENU, 50, 4 + 2 * 33 + 10, 0);
    CHECK_EQ(menus, 1);
    CHECK(idunn_list_view_selected(list, 2) && !idunn_list_view_selected(list, 0));
    CHECK_EQ(idunn_list_view_item_at(list, 50, 4 + 2 * 33 + 10), 2);
    CHECK_EQ(win->menu_x, 50);

    // on a selected item, the selection stays (a menu for all of them)
    key(win, IDUNN_KEY_A, IDUNN_MOD_CTRL);
    idunn_window_pointer(win, IDUNN_POINTER_MENU, 50, 4 + 10, 0);
    CHECK(idunn_list_view_selected(list, 0) && idunn_list_view_selected(list, 4));
    CHECK_EQ(menus, 2);

    // beside the items: none selected, the menu still comes
    idunn_window_pointer(win, IDUNN_POINTER_MENU, 50, 380, 0);
    CHECK(!idunn_list_view_selected(list, 0) && !idunn_list_view_selected(list, 2));
    CHECK_EQ(menus, 3);

    // a left press beside the items clears the selection too
    key(win, IDUNN_KEY_A, IDUNN_MOD_CTRL);
    idunn_window_tick(win, 50000);
    click_at(win, 50, 380, 0);
    CHECK(!idunn_list_view_selected(list, 1));

    // a box around the list takes a right click beside the list
    idunn_set_size(list, 0, 200);
    idunn_grow(list, 0);
    idunn_on_menu(col, count_menu, NULL);
    idunn_window_draw(win);
    int before = menus;
    idunn_window_pointer(win, IDUNN_POINTER_MENU, 50, 300, 0);
    CHECK_EQ(menus, before + 1);

    // Backspace is not the list's: the window's hook gets it
    window_keys = 0;
    key(win, IDUNN_KEY_BACKSPACE, 0);
    CHECK_EQ(window_keys, 1);
    idunn_window_free_widgets(win);
}

static void test_list(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 300, 200);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *list = idunn_list_view();
    static const char *const names[] = {"apple", "banana", "blueberry", "cherry", "date"};
    for (int i = 0; i < 100; i++)
        idunn_list_view_add(list, i % 2 ? "file" : "folder", names[i % 5], "1 KB");
    idunn_widget_t *view = idunn_grow(idunn_add(col, idunn_scroll_view(list)), 1);
    idunn_list_view_on_select(list, count_change, NULL);
    idunn_list_view_on_activate(list, count_activate, NULL);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // 100 rows of 32 with 1 between, 4 around
    CHECK_EQ(list->rect.h, 8 + 100 * 33 - 1);

    // a press selects; Down moves; End goes to the last row and scrolls to it
    changes = 0;
    click_at(win, 50, 4 + 33 + 10, 0);
    CHECK_EQ(idunn_list_view_current(list), 1);
    CHECK(idunn_list_view_selected(list, 1));
    key(win, IDUNN_KEY_DOWN, 0);
    CHECK_EQ(idunn_list_view_current(list), 2);
    CHECK(!idunn_list_view_selected(list, 1));
    key(win, IDUNN_KEY_END, 0);
    CHECK_EQ(idunn_list_view_current(list), 99);
    CHECK_EQ(idunn_scroll_view_offset(view), list->rect.h - 200);
    key(win, IDUNN_KEY_HOME, 0);
    CHECK_EQ(idunn_scroll_view_offset(view), 0);
    CHECK_EQ(changes, 4);

    // type to find: "b" then "b" again goes on; "bl" finds blueberry
    idunn_window_tick(win, 10000);
    idunn_window_key(win, 0, 0, "b");
    CHECK_EQ(idunn_list_view_current(list), 1);
    idunn_window_key(win, 0, 0, "b");
    CHECK_EQ(idunn_list_view_current(list), 2);
    idunn_window_tick(win, 12000);
    idunn_window_key(win, 0, 0, "b");
    idunn_window_key(win, 0, 0, "l");
    CHECK_EQ(idunn_list_view_current(list), 7);
    CHECK(strcmp(idunn_list_view_text(list, 7), "blueberry") == 0);

    // Enter and a double click activate
    activations = 0;
    key(win, IDUNN_KEY_ENTER, 0);
    idunn_window_tick(win, 20000);
    click_at(win, 50, 4 + 10, 0);
    click_at(win, 50, 4 + 10, 0);
    CHECK_EQ(activations, 2);

    // many: Ctrl toggles, Shift selects a range, Ctrl+A all
    idunn_list_view_set_multiple(list, true);
    idunn_window_tick(win, 30000);
    click_at(win, 50, 4 + 10, 0);
    click_at(win, 50, 4 + 2 * 33 + 10, IDUNN_MOD_CTRL);
    CHECK(idunn_list_view_selected(list, 0) && idunn_list_view_selected(list, 2));
    CHECK(!idunn_list_view_selected(list, 1));
    idunn_window_tick(win, 31000);
    click_at(win, 50, 4 + 4 * 33 + 10, IDUNN_MOD_SHIFT);
    CHECK(idunn_list_view_selected(list, 3) && idunn_list_view_selected(list, 4));
    CHECK(!idunn_list_view_selected(list, 0));
    key(win, IDUNN_KEY_A, IDUNN_MOD_CTRL);
    CHECK(idunn_list_view_selected(list, 0) && idunn_list_view_selected(list, 99));
    key(win, IDUNN_KEY_DOWN, IDUNN_MOD_SHIFT);
    CHECK(idunn_list_view_selected(list, 5) && !idunn_list_view_selected(list, 6));

    // the selected rows have the selection colour; it is drawn in the view
    idunn_window_tick(win, 40000);
    idunn_window_draw(win);
    gfx_color_t selection = idunn_color(&ctx->theme, IDUNN_COLOR_SELECTION);
    gfx_rect_t row3 = gfx_rect(4, list->rect.y + 4 + 3 * 33, 292, 32);
    gfx_color_t want = gfx_over(idunn_color(&ctx->theme, IDUNN_COLOR_SURFACE), selection);
    CHECK(*gfx_pixel_at(win->buffer, row3.x + 6, row3.y + 16) == want);

    idunn_window_free_widgets(win);
    free(win);
}

static void test_grid(idunn_context_t *ctx)
{
    idunn_window_t *win = idunn_window_offscreen(ctx, 420, 300);
    idunn_widget_t *col = idunn_column(0, 0);
    idunn_widget_t *grid = idunn_grid_view();
    for (int i = 0; i < 10; i++)
        idunn_list_view_add(grid, i < 3 ? "folder" : "text-x-generic", "notes.txt", NULL);
    idunn_add(col, idunn_scroll_view(grid));
    idunn_grow(col->children[0], 1);
    idunn_window_set_root(win, col);
    idunn_window_draw(win);

    // tiles of 96 by 88 with 4 between: 4 in a line of 420, so 3 lines
    int32_t gw;
    int32_t gh;
    idunn_measure(grid, 420, &gw, &gh);
    CHECK_EQ(gh, 8 + 3 * 92 - 4);

    // Right, Down (a line), and a press on the second tile of the second line
    click_at(win, 4 + 10, 4 + 10, 0);
    key(win, IDUNN_KEY_RIGHT, 0);
    CHECK_EQ(idunn_list_view_current(grid), 1);
    key(win, IDUNN_KEY_DOWN, 0);
    CHECK_EQ(idunn_list_view_current(grid), 5);
    click_at(win, 4 + 100 + 10, 4 + 92 + 10, 0);
    CHECK_EQ(idunn_list_view_current(grid), 5);
    click_at(win, 4 + 96 + 2, 4 + 10, 0);
    CHECK_EQ(idunn_list_view_current(grid), 5);

    // the picture is drawn: a folder is not the surface colour
    idunn_window_draw(win);
    gfx_color_t surface = idunn_color(&ctx->theme, IDUNN_COLOR_SURFACE);
    CHECK(*gfx_pixel_at(win->buffer, 4 + 48, 4 + 8 + 30) != surface);

    idunn_window_free_widgets(win);
    free(win);
}

void test_lists(idunn_context_t *ctx)
{
    test_scroll(ctx);
    test_hidden(ctx);
    test_tabs(ctx);
    test_list(ctx);
    test_list_menu(ctx);
    test_middle_click(ctx);
    test_single_click(ctx);
    test_edit_in_place(ctx);
    test_keep_end(ctx);
    test_grid(ctx);
}
