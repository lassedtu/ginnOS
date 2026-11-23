/**
 * @file buffer.c
 * @brief Saga: the text, as an array of lines, with undo and redo.
 */

#include "buffer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool line_reserve(line_t *l, size_t need)
{
    if (need + 1 <= l->capacity)
        return true;
    size_t cap = l->capacity ? l->capacity : 16;
    while (cap < need + 1)
        cap *= 2;
    char *more = realloc(l->text, cap);
    if (!more)
        return false;
    l->text = more;
    l->capacity = cap;
    return true;
}

static bool line_set(line_t *l, const char *text, size_t length)
{
    if (!line_reserve(l, length))
        return false;
    memcpy(l->text, text, length);
    l->length = length;
    l->text[length] = '\0';
    return true;
}

/** room for count more lines at index at (they are empty) */
static bool open_lines(buffer_t *b, size_t at, size_t count)
{
    if (b->count + count > b->capacity)
    {
        size_t cap = b->capacity ? b->capacity : 64;
        while (cap < b->count + count)
            cap *= 2;
        line_t *more = realloc(b->lines, cap * sizeof(line_t));
        if (!more)
            return false;
        b->lines = more;
        b->capacity = cap;
    }
    memmove(b->lines + at + count, b->lines + at, (b->count - at) * sizeof(line_t));
    memset(b->lines + at, 0, count * sizeof(line_t));
    b->count += count;
    return true;
}

static void close_lines(buffer_t *b, size_t at, size_t count)
{
    for (size_t i = at; i < at + count; i++)
        free(b->lines[i].text);
    memmove(b->lines + at, b->lines + at + count, (b->count - at - count) * sizeof(line_t));
    b->count -= count;
}

static void steps_clear(steps_t *s)
{
    for (size_t i = 0; i < s->count; i++)
        free(s->steps[i].text);
    s->count = 0;
}

static bool steps_push(steps_t *s, step_t step)
{
    if (s->count == s->capacity)
    {
        size_t cap = s->capacity ? s->capacity * 2 : 64;
        step_t *more = realloc(s->steps, cap * sizeof(step_t));
        if (!more)
            return false;
        s->steps = more;
        s->capacity = cap;
    }
    s->steps[s->count++] = step;
    return true;
}

bool buffer_init(buffer_t *b)
{
    memset(b, 0, sizeof(*b));
    b->saved_valid = true;
    return open_lines(b, 0, 1) && line_set(&b->lines[0], "", 0);
}

void buffer_free(buffer_t *b)
{
    for (size_t i = 0; i < b->count; i++)
        free(b->lines[i].text);
    free(b->lines);
    steps_clear(&b->undo);
    steps_clear(&b->redo);
    free(b->undo.steps);
    free(b->redo.steps);
    memset(b, 0, sizeof(*b));
}

/** the text as lines, without touching the undo history */
static pos_t raw_insert(buffer_t *b, pos_t at, const char *text, size_t length)
{
    line_t *first = &b->lines[at.line];
    // the rest of the first line goes after the inserted text
    size_t tail_length = first->length - at.col;
    char *tail = malloc(tail_length + 1);
    if (!tail)
        return at;
    memcpy(tail, first->text + at.col, tail_length);
    first->length = at.col;

    pos_t p = at;
    size_t start = 0;
    for (size_t i = 0; i <= length; i++)
    {
        bool end = i == length;
        if (!end && text[i] != '\n')
            continue;
        size_t piece = i - start;
        if (piece && text[start + piece - 1] == '\r' && !end)
            piece--; // CR LF: the CR goes
        line_t *l = &b->lines[p.line];
        if (line_reserve(l, l->length + piece))
        {
            memcpy(l->text + l->length, text + start, piece);
            l->length += piece;
            l->text[l->length] = '\0';
        }
        p.col = l->length;
        if (!end && open_lines(b, p.line + 1, 1))
        {
            p.line++;
            p.col = 0;
            line_set(&b->lines[p.line], "", 0);
        }
        start = i + 1;
    }
    line_t *last = &b->lines[p.line];
    if (line_reserve(last, last->length + tail_length))
    {
        memcpy(last->text + last->length, tail, tail_length);
        last->length += tail_length;
        last->text[last->length] = '\0';
    }
    free(tail);
    return p;
}

static void raw_delete(buffer_t *b, pos_t from, pos_t to)
{
    line_t *first = &b->lines[from.line];
    line_t *last = &b->lines[to.line];
    size_t rest = last->length - to.col;
    if (!line_reserve(first, from.col + rest))
        return;
    memmove(first->text + from.col, b->lines[to.line].text + to.col, rest);
    first->length = from.col + rest;
    first->text[first->length] = '\0';
    if (to.line > from.line)
        close_lines(b, from.line + 1, to.line - from.line);
}

bool buffer_set_text(buffer_t *b, const char *text, size_t length)
{
    for (size_t i = 0; i < b->count; i++)
        free(b->lines[i].text);
    b->count = 0;
    steps_clear(&b->undo);
    steps_clear(&b->redo);
    b->crlf = false;
    for (size_t i = 0; i + 1 < length && !b->crlf; i++)
        b->crlf = text[i] == '\r' && text[i + 1] == '\n';
    if (!open_lines(b, 0, 1) || !line_set(&b->lines[0], "", 0))
        return false;
    raw_insert(b, (pos_t){0, 0}, text, length);
    b->typing = false;
    b->group++;
    b->saved_at = 0;
    b->saved_valid = true;
    return true;
}

char *buffer_range(const buffer_t *b, pos_t from, pos_t to, size_t *length)
{
    if (pos_compare(from, to) > 0)
    {
        pos_t t = from;
        from = to;
        to = t;
    }
    size_t total = 0;
    for (size_t l = from.line; l <= to.line; l++)
        total += b->lines[l].length + 1;
    char *out = malloc(total + 1);
    if (!out)
        return NULL;
    size_t n = 0;
    for (size_t l = from.line; l <= to.line; l++)
    {
        size_t start = l == from.line ? from.col : 0;
        size_t end = l == to.line ? to.col : b->lines[l].length;
        memcpy(out + n, b->lines[l].text + start, end - start);
        n += end - start;
        if (l != to.line)
            out[n++] = '\n';
    }
    out[n] = '\0';
    if (length)
        *length = n;
    return out;
}

char *buffer_text(const buffer_t *b, size_t *length)
{
    size_t total = 0;
    for (size_t l = 0; l < b->count; l++)
        total += b->lines[l].length + 2;
    char *out = malloc(total + 1);
    if (!out)
        return NULL;
    size_t n = 0;
    for (size_t l = 0; l < b->count; l++)
    {
        memcpy(out + n, b->lines[l].text, b->lines[l].length);
        n += b->lines[l].length;
        if (l + 1 < b->count)
        {
            if (b->crlf)
                out[n++] = '\r';
            out[n++] = '\n';
        }
    }
    out[n] = '\0';
    if (length)
        *length = n;
    return out;
}

/** a new step: the redo history goes, and the saved state may be lost */
static void record(buffer_t *b, bool insert, pos_t at, const char *text, size_t length)
{
    if (b->saved_at > b->undo.count)
        b->saved_valid = false; // the saved text was in the redo history
    steps_clear(&b->redo);
    char *copy = malloc(length + 1);
    if (!copy)
        return;
    memcpy(copy, text, length);
    copy[length] = '\0';
    steps_push(&b->undo, (step_t){insert, at, copy, length, b->group});
}

pos_t buffer_insert(buffer_t *b, pos_t at, const char *text, size_t length, bool typing)
{
    at = buffer_clamp(b, at);
    if (length == 0)
        return at;
    // more typing joins the step before it, unless the text was saved there
    // or the typing goes on after a space
    step_t *last = b->undo.count ? &b->undo.steps[b->undo.count - 1] : NULL;
    bool join = typing && b->typing && last && last->insert && b->redo.count == 0 &&
                !(b->saved_valid && b->saved_at == b->undo.count) && last->length &&
                last->text[last->length - 1] != ' ' && memchr(text, '\n', length) == NULL;
    pos_t end = raw_insert(b, at, text, length);
    if (join)
    {
        char *more = realloc(last->text, last->length + length + 1);
        if (more)
        {
            memcpy(more + last->length, text, length);
            last->length += length;
            more[last->length] = '\0';
            last->text = more;
        }
    }
    else
    {
        b->group++; // a step of its own (a typed word after a space too)
        // the inserted text as it is in the buffer (LF line ends)
        size_t n;
        char *stored = buffer_range(b, at, end, &n);
        if (stored)
        {
            record(b, true, at, stored, n);
            free(stored);
        }
    }
    b->typing = typing;
    return end;
}

void buffer_delete(buffer_t *b, pos_t from, pos_t to)
{
    from = buffer_clamp(b, from);
    to = buffer_clamp(b, to);
    if (pos_compare(from, to) > 0)
    {
        pos_t t = from;
        from = to;
        to = t;
    }
    if (pos_compare(from, to) == 0)
        return;
    size_t n;
    char *gone = buffer_range(b, from, to, &n);
    b->group++;
    if (gone)
    {
        record(b, false, from, gone, n);
        free(gone);
    }
    raw_delete(b, from, to);
    b->typing = false;
}

void buffer_join_steps(buffer_t *b, size_t mark)
{
    if (mark >= b->undo.count)
        return;
    unsigned group = b->undo.steps[mark].group;
    for (size_t i = mark; i < b->undo.count; i++)
        b->undo.steps[i].group = group;
    b->typing = false;
    b->group++;
}

void buffer_break_group(buffer_t *b)
{
    b->typing = false;
    b->group++;
}

/** the position after a stored text that starts at at */
static pos_t end_of(pos_t at, const char *text, size_t length)
{
    pos_t p = at;
    for (size_t i = 0; i < length; i++)
    {
        if (text[i] == '\n')
        {
            p.line++;
            p.col = 0;
        }
        else
            p.col++;
    }
    return p;
}

/** do a step (forward: as it was done; else take it back) */
static pos_t apply(buffer_t *b, const step_t *s, bool forward)
{
    pos_t end = end_of(s->at, s->text, s->length);
    if (s->insert == forward)
        return raw_insert(b, s->at, s->text, s->length);
    raw_delete(b, s->at, end);
    return s->at;
}

static bool move_group(buffer_t *b, steps_t *from, steps_t *to, bool forward, pos_t *caret)
{
    if (from->count == 0)
        return false;
    unsigned group = from->steps[from->count - 1].group;
    while (from->count && from->steps[from->count - 1].group == group)
    {
        step_t s = from->steps[--from->count];
        pos_t p = apply(b, &s, forward);
        if (caret)
            *caret = p;
        steps_push(to, s);
    }
    b->typing = false;
    return true;
}

bool buffer_undo(buffer_t *b, pos_t *caret)
{
    return move_group(b, &b->undo, &b->redo, false, caret);
}

bool buffer_redo(buffer_t *b, pos_t *caret)
{
    return move_group(b, &b->redo, &b->undo, true, caret);
}

bool buffer_modified(const buffer_t *b)
{
    return !b->saved_valid || b->saved_at != b->undo.count;
}

void buffer_mark_saved(buffer_t *b)
{
    b->saved_at = b->undo.count;
    b->saved_valid = true;
    b->typing = false;
}

int pos_compare(pos_t a, pos_t b)
{
    if (a.line != b.line)
        return a.line < b.line ? -1 : 1;
    if (a.col != b.col)
        return a.col < b.col ? -1 : 1;
    return 0;
}

static bool continuation(char c)
{
    return ((unsigned char)c & 0xC0) == 0x80;
}

pos_t buffer_clamp(const buffer_t *b, pos_t p)
{
    if (p.line >= b->count)
        return buffer_end(b);
    const line_t *l = &b->lines[p.line];
    if (p.col > l->length)
        p.col = l->length;
    while (p.col > 0 && p.col < l->length && continuation(l->text[p.col]))
        p.col--;
    return p;
}

pos_t buffer_end(const buffer_t *b)
{
    return (pos_t){b->count - 1, b->lines[b->count - 1].length};
}

pos_t buffer_next(const buffer_t *b, pos_t p)
{
    const line_t *l = &b->lines[p.line];
    if (p.col < l->length)
    {
        p.col++;
        while (p.col < l->length && continuation(l->text[p.col]))
            p.col++;
        return p;
    }
    if (p.line + 1 < b->count)
        return (pos_t){p.line + 1, 0};
    return p;
}

pos_t buffer_prev(const buffer_t *b, pos_t p)
{
    if (p.col > 0)
    {
        const line_t *l = &b->lines[p.line];
        p.col--;
        while (p.col > 0 && continuation(l->text[p.col]))
            p.col--;
        return p;
    }
    if (p.line > 0)
        return (pos_t){p.line - 1, b->lines[p.line - 1].length};
    return p;
}

static bool word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || ((unsigned char)c & 0x80);
}

pos_t buffer_word_end(const buffer_t *b, pos_t p)
{
    const line_t *l = &b->lines[p.line];
    if (p.col == l->length)
        return buffer_next(b, p);
    while (p.col < l->length && !word_char(l->text[p.col]))
        p.col++;
    while (p.col < l->length && word_char(l->text[p.col]))
        p.col++;
    return p;
}

pos_t buffer_word_start(const buffer_t *b, pos_t p)
{
    if (p.col == 0)
        return buffer_prev(b, p);
    const line_t *l = &b->lines[p.line];
    while (p.col > 0 && !word_char(l->text[p.col - 1]))
        p.col--;
    while (p.col > 0 && word_char(l->text[p.col - 1]))
        p.col--;
    return p;
}

static bool match_at(const line_t *l, size_t col, const char *text, size_t n, bool match_case)
{
    if (col + n > l->length)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        char a = l->text[col + i];
        char c = text[i];
        if (match_case ? a != c : tolower((unsigned char)a) != tolower((unsigned char)c))
            return false;
    }
    return true;
}

bool buffer_find(const buffer_t *b, const char *text, bool match_case, pos_t from, bool backward,
                 pos_t *start, pos_t *end)
{
    size_t n = strlen(text);
    if (n == 0 || strchr(text, '\n'))
        return false;
    from = buffer_clamp(b, from);
    // every line once, from the start line round to it again
    for (size_t k = 0; k <= b->count; k++)
    {
        size_t line = backward ? (from.line + b->count - k % b->count) % b->count
                               : (from.line + k) % b->count;
        const line_t *l = &b->lines[line];
        if (!backward)
        {
            size_t col = k == 0 ? from.col : 0;
            for (; col + n <= l->length; col++)
            {
                if (k == b->count && col >= from.col)
                    break;
                if (match_at(l, col, text, n, match_case))
                {
                    *start = (pos_t){line, col};
                    *end = (pos_t){line, col + n};
                    return true;
                }
            }
        }
        else
        {
            size_t limit = k == 0 ? from.col : l->length;
            for (size_t col = limit + 1; col-- > 0;)
            {
                if (col + n > (k == 0 ? from.col : l->length))
                    continue;
                if (match_at(l, col, text, n, match_case))
                {
                    *start = (pos_t){line, col};
                    *end = (pos_t){line, col + n};
                    return true;
                }
            }
        }
    }
    return false;
}
