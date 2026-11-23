/**
 * @file find.c
 * @brief Saga: the find bar (Ctrl+F) and its replace row (Ctrl+H).
 *
 * typing in the find field selects the first match from the caret on;
 * Enter and F3 go to the next one, Shift+F3 to the one before. Escape
 * closes the bar.
 */

#include "saga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct
{
    idunn_widget_t *bar;
    idunn_widget_t *find_row;
    idunn_widget_t *replace_row;
    idunn_widget_t *find;
    idunn_widget_t *replace;
    idunn_widget_t *case_button;
    idunn_widget_t *result;
    bool match_case;
} F;

static void say(const char *text)
{
    idunn_label_set_text(F.result, text);
}

/** select the match from a position on; false when there is none */
static bool find_from(pos_t from, bool backward)
{
    const char *text = idunn_text_field_text(F.find);
    if (!text[0])
    {
        say(" ");
        return false;
    }
    pos_t start, end;
    if (!buffer_find(&S.buffer, text, F.match_case, from, backward, &start, &end))
    {
        say("Not found");
        idunn_set_error(F.find, true);
        return false;
    }
    idunn_set_error(F.find, false);
    say(" ");
    editor_select(start, end);
    return true;
}

void find_next(bool backward)
{
    if (!F.bar || idunn_text_field_text(F.find)[0] == '\0')
    {
        find_show(false);
        return;
    }
    pos_t start, end;
    if (!editor_selection(&start, &end))
        start = end = editor_caret();
    find_from(backward ? start : end, backward);
}

static void typed(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    // from the start of the selection, so the match grows as it is typed
    pos_t start, end;
    if (!editor_selection(&start, &end))
        start = editor_caret();
    find_from(start, false);
}

static void submitted(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    find_next(false);
}

static void next_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    find_next(data != NULL);
}

static void case_clicked(idunn_widget_t *b, void *data)
{
    (void)data;
    F.match_case = !F.match_case;
    idunn_button_set_variant(b, F.match_case ? IDUNN_SECONDARY : IDUNN_GHOST);
    typed(NULL, NULL);
}

/** true when the selection is a match of the find field */
static bool on_match(pos_t *start, pos_t *end)
{
    if (!editor_selection(start, end))
        return false;
    const char *text = idunn_text_field_text(F.find);
    pos_t s, e;
    return text[0] && buffer_find(&S.buffer, text, F.match_case, *start, false, &s, &e) &&
           pos_compare(s, *start) == 0 && pos_compare(e, *end) == 0;
}

static void replace_one(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    pos_t start, end;
    if (on_match(&start, &end))
    {
        const char *with = idunn_text_field_text(F.replace);
        buffer_break_group(&S.buffer);
        editor_replace(with, strlen(with));
    }
    find_next(false);
}

static void replace_all(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    const char *text = idunn_text_field_text(F.find);
    const char *with = idunn_text_field_text(F.replace);
    size_t with_length = strlen(with);
    if (!text[0])
        return;
    size_t mark = S.buffer.undo.count;
    pos_t from = {0, 0};
    pos_t start, end;
    int count = 0;
    // from the start on; a match before the last one means it went round
    while (buffer_find(&S.buffer, text, F.match_case, from, false, &start, &end) &&
           pos_compare(start, from) >= 0)
    {
        buffer_delete(&S.buffer, start, end);
        from = buffer_insert(&S.buffer, start, with, with_length, false);
        count++;
    }
    buffer_join_steps(&S.buffer, mark);
    char result[48];
    snprintf(result, sizeof(result), count == 1 ? "1 replaced" : "%d replaced", count);
    say(result);
    editor_select(from, from);
}

static void close_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    find_hide();
}

static idunn_widget_t *icon(idunn_widget_t *row, const char *name, const char *tip,
                            idunn_callback_t on_click, void *data)
{
    idunn_widget_t *b = idunn_add(row, idunn_icon_button(name, IDUNN_GHOST, on_click, data));
    idunn_button_set_size(b, IDUNN_SIZE_SMALL);
    idunn_set_tooltip(b, tip);
    return b;
}

idunn_widget_t *find_bar_new(void)
{
    F.bar = idunn_column(1, 0);
    idunn_box_set_padding(F.bar, 2, 3);

    F.find_row = idunn_add(F.bar, idunn_row(1, 0));
    idunn_box_set_align(F.find_row, IDUNN_CENTER, IDUNN_START);
    F.find = idunn_grow(idunn_add(F.find_row, idunn_text_field("Find")), 1);
    idunn_text_field_set_icon(F.find, "search");
    idunn_text_field_on_change(F.find, typed, NULL);
    idunn_text_field_on_submit(F.find, submitted, NULL);
    F.result = idunn_add(F.find_row, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(F.result, IDUNN_COLOR_MUTED_FOREGROUND);
    F.case_button = icon(F.find_row, "case-sensitive", "Match case", case_clicked, NULL);
    icon(F.find_row, "chevron-up", "The one before (Shift+F3)", next_clicked, (void *)1);
    icon(F.find_row, "chevron-down", "The next one (F3)", next_clicked, NULL);
    icon(F.find_row, "x", "Close (Escape)", close_clicked, NULL);

    F.replace_row = idunn_add(F.bar, idunn_row(1, 0));
    idunn_box_set_align(F.replace_row, IDUNN_CENTER, IDUNN_START);
    F.replace = idunn_grow(idunn_add(F.replace_row, idunn_text_field("Replace with")), 1);
    idunn_text_field_set_icon(F.replace, "replace");
    idunn_widget_t *one = idunn_add(F.replace_row, idunn_button("Replace", IDUNN_OUTLINE,
                                                                replace_one, NULL));
    idunn_button_set_size(one, IDUNN_SIZE_SMALL);
    idunn_widget_t *all = idunn_add(F.replace_row, idunn_button("All", IDUNN_OUTLINE,
                                                                replace_all, NULL));
    idunn_button_set_size(all, IDUNN_SIZE_SMALL);

    idunn_set_hidden(F.bar, true);
    return F.bar;
}

void find_show(bool replace)
{
    idunn_set_hidden(F.bar, false);
    idunn_set_hidden(F.replace_row, !replace);
    // the selected text (one line) is what to find
    pos_t start, end;
    if (editor_selection(&start, &end) && start.line == end.line)
    {
        size_t n;
        char *text = buffer_range(&S.buffer, start, end, &n);
        if (text)
            idunn_text_field_set_text(F.find, text);
        free(text);
    }
    idunn_set_focus(F.find);
    idunn_text_field_select(F.find, 0, (size_t)-1);
}

void find_hide(void)
{
    idunn_set_hidden(F.bar, true);
    idunn_set_error(F.find, false);
    editor_focus();
}
