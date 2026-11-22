/**
 * @file text_edit.c
 * @brief idunn: the text, the caret and the selection of an editable
 *        text, and the keys that act on them the same way in a text field
 *        and a text area.
 *
 * offsets are bytes of UTF-8 and always at the start of a character. the
 * selection is from the anchor to the caret (empty when they are equal).
 */

#include "idunn_internal.h"

#include <stdlib.h>
#include <string.h>

static bool continuation(char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

bool idunn_edit_set(idunn_edit_t *e, const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy)
        return false;
    memcpy(copy, text, length);
    copy[length] = '\0';
    free(e->text);
    e->text = copy;
    e->length = length;
    e->capacity = length + 1;
    e->caret = e->anchor = length;
    return true;
}

void idunn_edit_free(idunn_edit_t *e)
{
    free(e->text);
    *e = (idunn_edit_t){0};
}

size_t idunn_edit_start(const idunn_edit_t *e)
{
    return e->caret < e->anchor ? e->caret : e->anchor;
}

size_t idunn_edit_end(const idunn_edit_t *e)
{
    return e->caret > e->anchor ? e->caret : e->anchor;
}

size_t idunn_edit_prev(const idunn_edit_t *e, size_t pos)
{
    if (pos == 0)
        return 0;
    do
        pos--;
    while (pos > 0 && continuation(e->text[pos]));
    return pos;
}

size_t idunn_edit_next(const idunn_edit_t *e, size_t pos)
{
    if (pos >= e->length)
        return e->length;
    do
        pos++;
    while (pos < e->length && continuation(e->text[pos]));
    return pos;
}

size_t idunn_edit_word_left(const idunn_edit_t *e, size_t pos)
{
    while (pos > 0 && space(e->text[pos - 1]))
        pos--;
    while (pos > 0 && !space(e->text[pos - 1]))
        pos--;
    return pos;
}

size_t idunn_edit_word_right(const idunn_edit_t *e, size_t pos)
{
    while (pos < e->length && space(e->text[pos]))
        pos++;
    while (pos < e->length && !space(e->text[pos]))
        pos++;
    return pos;
}

void idunn_edit_move(idunn_edit_t *e, size_t pos, bool extend)
{
    e->caret = pos > e->length ? e->length : pos;
    if (!extend)
        e->anchor = e->caret;
}

void idunn_edit_select_word(idunn_edit_t *e, size_t pos)
{
    // the word at pos, or the spaces there
    if (pos > e->length)
        pos = e->length;
    bool on_space = pos < e->length ? space(e->text[pos]) : pos > 0 && space(e->text[pos - 1]);
    size_t start = pos;
    size_t end = pos;
    while (start > 0 && space(e->text[start - 1]) == on_space)
        start--;
    while (end < e->length && space(e->text[end]) == on_space)
        end++;
    e->anchor = start;
    e->caret = end;
}

bool idunn_edit_insert(idunn_edit_t *e, const char *text, size_t length)
{
    size_t start = idunn_edit_start(e);
    size_t end = idunn_edit_end(e);
    size_t new_length = e->length - (end - start) + length;
    if (new_length + 1 > e->capacity)
    {
        size_t capacity = e->capacity ? e->capacity : 32;
        while (capacity < new_length + 1)
            capacity *= 2;
        char *grown = realloc(e->text, capacity);
        if (!grown)
            return false;
        e->text = grown;
        e->capacity = capacity;
    }
    memmove(e->text + start + length, e->text + end, e->length - end + 1);
    memcpy(e->text + start, text, length);
    e->length = new_length;
    e->caret = e->anchor = start + length;
    return true;
}

void idunn_edit_delete(idunn_edit_t *e, bool forward, bool word)
{
    if (e->caret == e->anchor)
    {
        if (forward)
            e->caret = word ? idunn_edit_word_right(e, e->caret) : idunn_edit_next(e, e->caret);
        else
            e->caret = word ? idunn_edit_word_left(e, e->caret) : idunn_edit_prev(e, e->caret);
    }
    idunn_edit_insert(e, "", 0);
}

idunn_edit_result_t idunn_edit_key(idunn_widget_t *w, idunn_edit_t *e, const idunn_event_t *ev,
                                   bool multiline)
{
    bool shift = ev->mods & IDUNN_MOD_SHIFT;
    bool ctrl = ev->mods & IDUNN_MOD_CTRL;
    size_t start = idunn_edit_start(e);
    size_t end = idunn_edit_end(e);
    bool selected = start != end;

    switch (ev->code)
    {
    case IDUNN_KEY_LEFT:
        if (selected && !shift && !ctrl)
            idunn_edit_move(e, start, false);
        else
            idunn_edit_move(e, ctrl ? idunn_edit_word_left(e, e->caret) : idunn_edit_prev(e, e->caret),
                            shift);
        return IDUNN_EDIT_MOVED;
    case IDUNN_KEY_RIGHT:
        if (selected && !shift && !ctrl)
            idunn_edit_move(e, end, false);
        else
            idunn_edit_move(e,
                            ctrl ? idunn_edit_word_right(e, e->caret) : idunn_edit_next(e, e->caret),
                            shift);
        return IDUNN_EDIT_MOVED;
    case IDUNN_KEY_HOME:
        idunn_edit_move(e, 0, shift);
        return IDUNN_EDIT_MOVED;
    case IDUNN_KEY_END:
        idunn_edit_move(e, e->length, shift);
        return IDUNN_EDIT_MOVED;
    case IDUNN_KEY_BACKSPACE:
    case IDUNN_KEY_DELETE:
        if (!selected && (ev->code == IDUNN_KEY_BACKSPACE ? e->caret == 0 : e->caret == e->length))
            return IDUNN_EDIT_USED;
        idunn_edit_delete(e, ev->code == IDUNN_KEY_DELETE, ctrl);
        return IDUNN_EDIT_CHANGED;
    case IDUNN_KEY_ENTER:
        if (!multiline)
            return IDUNN_EDIT_IGNORED;
        idunn_edit_insert(e, "\n", 1);
        return IDUNN_EDIT_CHANGED;
    default:
        break;
    }

    if (ctrl && !(ev->mods & IDUNN_MOD_ALT))
    {
        switch (ev->code)
        {
        case IDUNN_KEY_A:
            e->anchor = 0;
            e->caret = e->length;
            return IDUNN_EDIT_MOVED;
        case IDUNN_KEY_C:
        case IDUNN_KEY_X:
            if (!selected)
                return IDUNN_EDIT_USED;
            idunn_copy(w, e->text + start, end - start);
            if (ev->code == IDUNN_KEY_C)
                return IDUNN_EDIT_USED;
            idunn_edit_insert(e, "", 0);
            return IDUNN_EDIT_CHANGED;
        case IDUNN_KEY_V:
            idunn_paste(w); // a PASTE event brings the text
            return IDUNN_EDIT_USED;
        default:
            return IDUNN_EDIT_IGNORED;
        }
    }

    // typing: the characters the key makes
    if (ev->text && (unsigned char)ev->text[0] >= 0x20 && ev->text[0] != 0x7F)
    {
        idunn_edit_insert(e, ev->text, strlen(ev->text));
        return IDUNN_EDIT_CHANGED;
    }
    return IDUNN_EDIT_IGNORED;
}

bool idunn_edit_paste(idunn_edit_t *e, const char *text, size_t length, bool multiline)
{
    // a line of text takes the lines of the clipboard with spaces between
    char *clean = malloc(length + 1);
    if (!clean)
        return false;
    size_t n = 0;
    for (size_t i = 0; i < length; i++)
    {
        char c = text[i];
        if (c == '\r')
            continue;
        if (c == '\n' && !multiline)
            c = ' ';
        else if (c == '\t')
            c = ' ';
        else if ((unsigned char)c < 0x20 && c != '\n')
            continue;
        clean[n++] = c;
    }
    bool ok = idunn_edit_insert(e, clean, n);
    free(clean);
    return ok;
}
