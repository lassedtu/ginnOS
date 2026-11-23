/**
 * @file editor.c
 * @brief Saga: the editing canvas: line numbers, the text in JetBrains
 *        Mono, the selection and a blinking caret.
 *
 * the text does not wrap; long lines scroll sideways. a tab shows as the
 * spaces to the next column of four, and the Tab key types spaces.
 *
 *   arrows, Home, End, Page Up, Page Down    move (Shift: select)
 *   Ctrl+Left, Ctrl+Right                    a word at a time
 *   Ctrl+Home, Ctrl+End                      the start, the end
 *   Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V           all, copy, cut, paste
 *   Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z)            undo, redo
 *   Tab, Shift+Tab                           indent, take back (lines)
 *   Enter                                    a new line, with the indent
 */

#include "saga.h"

#include <bragi.h>
#include <conf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>

#define FONT       "/usr/share/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"
#define FONT_SIZE  13 // design pixels
#define FONT_MIN   9
#define FONT_MAX   32
#define BLINK_MS   500
#define WHEEL      3 // lines a wheel step scrolls
#define PAD        8 // design pixels left of the text and around the numbers

static struct
{
    bragi_font_t *font;
    bragi_face_t *face;
    int font_size; // design pixels
    int scale;
    int32_t cw;    // a cell
    int32_t ch;
    int32_t lh;    // a line: the cell and some room
    int32_t width; // the canvas, in pixels
    int32_t height;

    size_t top;     // the first line in view
    int32_t left;   // pixels of the text scrolled away on the left
    pos_t caret;
    pos_t anchor;   // the other end of the selection (the caret: none)
    int32_t goal;   // the column Up and Down keep (-1: the caret's)
    bool focused;
    bool blink_on;
    bool dragging;
} E;

static const idunn_theme_t *theme(void)
{
    return idunn_app_theme(S.app);
}

static gfx_color_t token(idunn_color_t c)
{
    const idunn_theme_t *t = theme();
    return t->colors[t->mode][c];
}

static int32_t px(int32_t design)
{
    return design * E.scale;
}

static void load_face(void)
{
    if (!E.font)
        E.font = bragi_font_open(FONT);
    if (!E.font)
        return;
    bragi_face_free(E.face);
    E.face = bragi_face_new(E.font, (float)(E.font_size * E.scale));
    if (!E.face)
        return;
    bragi_cell_size(E.face, &E.cw, &E.ch);
    E.lh = E.ch + px(4);
}

/** the column (in cells) of a byte offset in a line */
static int32_t column_of(size_t line, size_t col)
{
    const line_t *l = &S.buffer.lines[line];
    int32_t c = 0;
    for (size_t i = 0; i < col && i < l->length; i++)
    {
        unsigned char b = (unsigned char)l->text[i];
        if (b == '\t')
            c = (c / TAB_WIDTH + 1) * TAB_WIDTH;
        else if ((b & 0xC0) != 0x80)
            c++;
    }
    return c;
}

/** the byte offset nearest to a column */
static size_t col_at(size_t line, int32_t column)
{
    const line_t *l = &S.buffer.lines[line];
    int32_t c = 0;
    size_t i = 0;
    while (i < l->length)
    {
        unsigned char b = (unsigned char)l->text[i];
        int32_t next = b == '\t' ? (c / TAB_WIDTH + 1) * TAB_WIDTH : c + 1;
        if (next > column)
            return column - c > next - column ? buffer_next(&S.buffer, (pos_t){line, i}).col : i;
        c = next;
        i = buffer_next(&S.buffer, (pos_t){line, i}).col;
        if (i == 0)
            break; // the next line: the end
    }
    return l->length;
}

static int32_t gutter_width(void)
{
    int digits = 1;
    for (size_t n = S.buffer.count; n >= 10; n /= 10)
        digits++;
    if (digits < 3)
        digits = 3;
    return digits * E.cw + px(2 * PAD);
}

static int32_t visible_lines(void)
{
    int32_t n = E.lh ? E.height / E.lh : 1;
    return n < 1 ? 1 : n;
}

static void redraw(void)
{
    idunn_canvas_damage(S.canvas, gfx_rect(0, 0, E.width, E.height));
}

static void restart_blink(void)
{
    E.blink_on = true;
    if (E.focused)
        idunn_canvas_wake(S.canvas, BLINK_MS);
}

/** scroll so the caret is in view */
static void keep_caret_visible(void)
{
    int32_t rows = visible_lines();
    if (E.caret.line < E.top)
        E.top = E.caret.line;
    else if (E.caret.line >= E.top + (size_t)rows)
        E.top = E.caret.line - (size_t)rows + 1;
    int32_t x = column_of(E.caret.line, E.caret.col) * E.cw;
    int32_t room = E.width - gutter_width() - px(PAD) - E.cw;
    if (x - E.left > room)
        E.left = x - room + 8 * E.cw;
    if (x < E.left)
        E.left = x - 8 * E.cw;
    if (E.left < 0)
        E.left = 0;
}

/** the caret (and the selection) moved or the text changed */
static void moved(bool keep_goal)
{
    if (!keep_goal)
        E.goal = -1;
    keep_caret_visible();
    restart_blink();
    redraw();
    saga_changed();
}

static void move_to(pos_t p, bool extend)
{
    E.caret = buffer_clamp(&S.buffer, p);
    if (!extend)
        E.anchor = E.caret;
}

bool editor_selection(pos_t *start, pos_t *end)
{
    int c = pos_compare(E.anchor, E.caret);
    if (c == 0)
        return false;
    *start = c < 0 ? E.anchor : E.caret;
    *end = c < 0 ? E.caret : E.anchor;
    return true;
}

pos_t editor_caret(void)
{
    return E.caret;
}

void editor_select(pos_t start, pos_t end)
{
    E.anchor = buffer_clamp(&S.buffer, start);
    E.caret = buffer_clamp(&S.buffer, end);
    moved(false);
}

void editor_replace(const char *text, size_t length)
{
    pos_t start, end;
    if (editor_selection(&start, &end))
    {
        buffer_delete(&S.buffer, start, end);
        E.caret = start;
    }
    E.caret = buffer_insert(&S.buffer, E.caret, text, length, false);
    E.anchor = E.caret;
    moved(false);
}

static void type(const char *text)
{
    pos_t start, end;
    if (editor_selection(&start, &end))
    {
        buffer_delete(&S.buffer, start, end);
        E.caret = start;
    }
    E.caret = buffer_insert(&S.buffer, E.caret, text, strlen(text), true);
    E.anchor = E.caret;
    moved(false);
}

void editor_undo(void)
{
    pos_t p;
    if (buffer_undo(&S.buffer, &p))
    {
        move_to(p, false);
        moved(false);
    }
}

void editor_redo(void)
{
    pos_t p;
    if (buffer_redo(&S.buffer, &p))
    {
        move_to(p, false);
        moved(false);
    }
}

void editor_reset(void)
{
    E.caret = E.anchor = (pos_t){0, 0};
    E.top = 0;
    E.left = 0;
    moved(false);
}

void editor_focus(void)
{
    idunn_set_focus(S.canvas);
}

/** the text size of the settings ([editor] font_size in saga.conf) */
static int saved_font_size(void)
{
    conf_t *conf = conf_open("saga");
    long size = conf ? conf_get_int(conf, "editor", "font_size", FONT_SIZE) : FONT_SIZE;
    conf_free(conf);
    return size < FONT_MIN ? FONT_MIN : size > FONT_MAX ? FONT_MAX : (int)size;
}

void editor_zoom(int step)
{
    int size = step == 0 ? saved_font_size() : E.font_size + step;
    if (size < FONT_MIN || size > FONT_MAX)
        return;
    E.font_size = size;
    load_face();
    moved(true);
}

void editor_theme_changed(void)
{
    if (theme()->scale != E.scale)
    {
        E.scale = theme()->scale;
        load_face();
    }
    redraw();
}

static void copy_selection(bool cut)
{
    pos_t start, end;
    if (!editor_selection(&start, &end))
        return;
    size_t n;
    char *text = buffer_range(&S.buffer, start, end, &n);
    if (text)
        idunn_canvas_copy(S.canvas, text, n);
    free(text);
    if (cut)
    {
        buffer_delete(&S.buffer, start, end);
        move_to(start, false);
        moved(false);
    }
}

/** the leading spaces and tabs of a line, up to max bytes */
static size_t indent_of(size_t line, size_t max)
{
    const line_t *l = &S.buffer.lines[line];
    size_t n = 0;
    while (n < l->length && n < max && (l->text[n] == ' ' || l->text[n] == '\t'))
        n++;
    return n;
}

static void new_line(void)
{
    size_t n = indent_of(E.caret.line, E.caret.col);
    char *text = malloc(n + 2);
    if (!text)
        return;
    text[0] = '\n';
    memcpy(text + 1, S.buffer.lines[E.caret.line].text, n);
    text[n + 1] = '\0';
    buffer_break_group(&S.buffer);
    editor_replace(text, n + 1);
    free(text);
}

/** Tab with lines selected: indent them; Shift+Tab: take an indent back */
static void indent_lines(bool back)
{
    pos_t start, end;
    if (!editor_selection(&start, &end))
        start = end = E.caret;
    size_t last = end.line > start.line && end.col == 0 ? end.line - 1 : end.line;
    size_t mark = S.buffer.undo.count;
    for (size_t l = start.line; l <= last; l++)
    {
        if (back)
        {
            size_t n = indent_of(l, TAB_WIDTH);
            if (n && S.buffer.lines[l].text[0] == '\t')
                n = 1;
            if (n)
                buffer_delete(&S.buffer, (pos_t){l, 0}, (pos_t){l, n});
        }
        else if (S.buffer.lines[l].length)
            buffer_insert(&S.buffer, (pos_t){l, 0}, "    ", TAB_WIDTH, false);
    }
    buffer_join_steps(&S.buffer, mark); // the lines undo together
    E.anchor = (pos_t){start.line, 0};
    E.caret = (pos_t){last, S.buffer.lines[last].length};
    moved(false);
}

static void tab(bool back)
{
    pos_t start, end;
    if (back || (editor_selection(&start, &end) && start.line != end.line))
    {
        indent_lines(back);
        return;
    }
    // spaces to the next column of four
    int32_t c = column_of(E.caret.line, E.caret.col);
    int n = TAB_WIDTH - c % TAB_WIDTH;
    char spaces[TAB_WIDTH + 1] = "    ";
    spaces[n] = '\0';
    type(spaces);
}

/** Backspace in the indent goes back to the column of four before */
static void backspace(bool word)
{
    pos_t start, end;
    if (editor_selection(&start, &end))
    {
        buffer_delete(&S.buffer, start, end);
        move_to(start, false);
        moved(false);
        return;
    }
    pos_t from;
    size_t n = indent_of(E.caret.line, E.caret.col);
    if (word)
        from = buffer_word_start(&S.buffer, E.caret);
    else if (E.caret.col > 0 && n == E.caret.col &&
             S.buffer.lines[E.caret.line].text[E.caret.col - 1] == ' ')
    {
        int32_t c = column_of(E.caret.line, E.caret.col);
        int32_t back = c % TAB_WIDTH ? c % TAB_WIDTH : TAB_WIDTH;
        from = (pos_t){E.caret.line, E.caret.col - (size_t)back};
    }
    else
        from = buffer_prev(&S.buffer, E.caret);
    buffer_delete(&S.buffer, from, E.caret);
    move_to(from, false);
    moved(false);
}

static void forward_delete(bool word)
{
    pos_t start, end;
    if (editor_selection(&start, &end))
    {
        buffer_delete(&S.buffer, start, end);
        move_to(start, false);
    }
    else
    {
        pos_t to = word ? buffer_word_end(&S.buffer, E.caret) : buffer_next(&S.buffer, E.caret);
        buffer_delete(&S.buffer, E.caret, to);
    }
    moved(false);
}

static void vertical(long lines, bool extend)
{
    if (E.goal < 0)
        E.goal = column_of(E.caret.line, E.caret.col);
    long line = (long)E.caret.line + lines;
    if (line < 0)
    {
        move_to((pos_t){0, 0}, extend);
    }
    else if (line >= (long)S.buffer.count)
    {
        move_to(buffer_end(&S.buffer), extend);
    }
    else
    {
        move_to((pos_t){(size_t)line, col_at((size_t)line, E.goal)}, extend);
    }
    moved(true);
}

static bool key(const idunn_input_t *in)
{
    bool ctrl = in->mods & IDUNN_MOD_CTRL;
    bool shift = in->mods & IDUNN_MOD_SHIFT;
    bool alt = in->mods & IDUNN_MOD_ALT;
    pos_t start, end;
    bool selected = editor_selection(&start, &end);
    const line_t *line = &S.buffer.lines[E.caret.line];

    switch (in->code)
    {
    case KEY_LEFT:
        if (selected && !shift)
            move_to(start, false);
        else
            move_to(ctrl ? buffer_word_start(&S.buffer, E.caret) : buffer_prev(&S.buffer, E.caret),
                    shift);
        moved(false);
        return true;
    case KEY_RIGHT:
        if (selected && !shift)
            move_to(end, false);
        else
            move_to(ctrl ? buffer_word_end(&S.buffer, E.caret) : buffer_next(&S.buffer, E.caret),
                    shift);
        moved(false);
        return true;
    case KEY_UP:
        vertical(-1, shift);
        return true;
    case KEY_DOWN:
        vertical(1, shift);
        return true;
    case KEY_PAGEUP:
        vertical(-(long)visible_lines() + 1, shift);
        return true;
    case KEY_PAGEDOWN:
        vertical(visible_lines() - 1, shift);
        return true;
    case KEY_HOME:
    {
        // the first character after the indent, then the line's start
        size_t indent = indent_of(E.caret.line, line->length);
        pos_t to = ctrl ? (pos_t){0, 0}
                        : (pos_t){E.caret.line, E.caret.col == indent ? 0 : indent};
        move_to(to, shift);
        moved(false);
        return true;
    }
    case KEY_END:
        move_to(ctrl ? buffer_end(&S.buffer) : (pos_t){E.caret.line, line->length}, shift);
        moved(false);
        return true;
    case KEY_BACKSPACE:
        backspace(ctrl);
        return true;
    case KEY_DELETE:
        forward_delete(ctrl);
        return true;
    case KEY_ENTER:
    case KEY_KPENTER:
        new_line();
        return true;
    case KEY_TAB:
        if (ctrl || alt)
            return false;
        tab(shift);
        return true;
    default:
        break;
    }

    if (ctrl && !alt)
    {
        switch (in->code)
        {
        case KEY_A:
            E.anchor = (pos_t){0, 0};
            E.caret = buffer_end(&S.buffer);
            moved(false);
            return true;
        case KEY_C:
            copy_selection(false);
            return true;
        case KEY_X:
            copy_selection(true);
            return true;
        case KEY_V:
            idunn_canvas_paste(S.canvas);
            return true;
        case KEY_Z:
            shift ? editor_redo() : editor_undo();
            return true;
        case KEY_Y:
            editor_redo();
            return true;
        default:
            return false; // the window's shortcuts (main.c)
        }
    }
    if (!alt && in->text && in->text[0] && (unsigned char)in->text[0] >= ' ' &&
        in->text[0] != 0x7F)
    {
        type(in->text);
        return true;
    }
    return false;
}

/** the text position at a point of the canvas */
static pos_t pos_at(int32_t x, int32_t y)
{
    long line = (long)E.top + (y < 0 ? -1 : y / E.lh);
    if (line < 0)
        return (pos_t){0, 0};
    if (line >= (long)S.buffer.count)
        return buffer_end(&S.buffer);
    int32_t tx = x - gutter_width() - px(PAD) + E.left;
    int32_t column = tx < 0 ? 0 : (tx + E.cw / 2) / E.cw;
    return (pos_t){(size_t)line, col_at((size_t)line, column)};
}

static void select_word(pos_t p)
{
    pos_t end = buffer_word_end(&S.buffer, p);
    pos_t start = buffer_word_start(&S.buffer, end);
    if (end.line != p.line)
        end = p;
    if (start.line != p.line)
        start = p;
    E.anchor = start;
    E.caret = end;
}

static bool input(idunn_widget_t *canvas, const idunn_input_t *in, void *data)
{
    (void)canvas;
    (void)data;
    switch (in->type)
    {
    case IDUNN_INPUT_KEY:
        return key(in);
    case IDUNN_INPUT_PASTE:
        if (in->length)
        {
            buffer_break_group(&S.buffer);
            editor_replace(in->text, in->length);
        }
        return true;
    case IDUNN_INPUT_PRESS:
    {
        idunn_set_focus(S.canvas);
        pos_t p = pos_at(in->x, in->y);
        if (in->clicks == 2)
            select_word(p);
        else if (in->clicks >= 3)
        {
            E.anchor = (pos_t){p.line, 0};
            E.caret = buffer_next(&S.buffer, (pos_t){p.line, S.buffer.lines[p.line].length});
        }
        else
            move_to(p, in->mods & IDUNN_MOD_SHIFT);
        E.dragging = true;
        moved(false);
        return true;
    }
    case IDUNN_INPUT_DRAG:
        if (E.dragging)
        {
            move_to(pos_at(in->x, in->y), true);
            moved(false);
        }
        return true;
    case IDUNN_INPUT_RELEASE:
        E.dragging = false;
        return true;
    case IDUNN_INPUT_SCROLL:
    {
        long top = (long)E.top - in->steps * WHEEL;
        long most = (long)S.buffer.count - visible_lines() / 2;
        E.top = (size_t)(top < 0 ? 0 : top > most ? (most < 0 ? 0 : most) : top);
        redraw();
        return true;
    }
    case IDUNN_INPUT_FOCUS:
        E.focused = true;
        restart_blink();
        redraw();
        return true;
    case IDUNN_INPUT_BLUR:
        E.focused = false;
        redraw();
        return true;
    case IDUNN_INPUT_TIMER:
        if (E.focused)
        {
            E.blink_on = !E.blink_on;
            idunn_canvas_wake(S.canvas, BLINK_MS);
            redraw();
        }
        return true;
    default:
        return false;
    }
}

/** draw one line's text, from its first character in view */
static void draw_text(gfx_buffer_t *buf, const line_t *l, int32_t x0, int32_t y,
                      gfx_color_t color)
{
    int32_t c = 0;
    int32_t first = E.left / E.cw;
    int32_t last = first + (E.width / E.cw) + 2;
    for (size_t i = 0; i < l->length && c <= last;)
    {
        unsigned char b = (unsigned char)l->text[i];
        uint32_t cp = b;
        size_t n = 1;
        if (b >= 0xF0 && i + 3 < l->length)
        {
            cp = (uint32_t)(b & 0x07) << 18 | (uint32_t)(l->text[i + 1] & 0x3F) << 12 |
                 (uint32_t)(l->text[i + 2] & 0x3F) << 6 | (uint32_t)(l->text[i + 3] & 0x3F);
            n = 4;
        }
        else if (b >= 0xE0 && i + 2 < l->length)
        {
            cp = (uint32_t)(b & 0x0F) << 12 | (uint32_t)(l->text[i + 1] & 0x3F) << 6 |
                 (uint32_t)(l->text[i + 2] & 0x3F);
            n = 3;
        }
        else if (b >= 0xC0 && i + 1 < l->length)
        {
            cp = (uint32_t)(b & 0x1F) << 6 | (uint32_t)(l->text[i + 1] & 0x3F);
            n = 2;
        }
        if (b == '\t')
        {
            c = (c / TAB_WIDTH + 1) * TAB_WIDTH;
        }
        else
        {
            if (c >= first - 1 && cp > ' ')
                bragi_draw_cell(buf, E.face, x0 + c * E.cw, y, cp, color);
            c++;
        }
        i += n;
    }
}

static void draw(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t rect, void *data)
{
    (void)canvas;
    (void)data;
    if (!E.face)
        return;
    gfx_fill_rect(buf, rect, token(IDUNN_COLOR_BACKGROUND));
    int32_t gw = gutter_width();
    int32_t text_x = rect.x + gw + px(PAD) - E.left;
    gfx_rect_t text_area = gfx_rect(rect.x + gw, rect.y, rect.w - gw, rect.h);
    pos_t start, end;
    bool selected = editor_selection(&start, &end);
    int32_t rows = visible_lines() + 1;
    int32_t text_top = (E.lh - E.ch) / 2;

    gfx_rect_t saved = buf->clip;
    buf->clip = gfx_rect_intersect(saved, text_area);
    for (int32_t r = 0; r < rows; r++)
    {
        size_t li = E.top + (size_t)r;
        if (li >= S.buffer.count)
            break;
        int32_t y = rect.y + r * E.lh;
        const line_t *l = &S.buffer.lines[li];
        // the caret's line, when nothing is selected
        if (li == E.caret.line && !selected)
            gfx_fill_rect(buf, gfx_rect(text_area.x, y, text_area.w, E.lh),
                          token(IDUNN_COLOR_HOVER));
        // the selection: to the end of the line, and one cell for its end
        if (selected && li >= start.line && li <= end.line)
        {
            int32_t a = li == start.line ? column_of(li, start.col) : 0;
            int32_t z = li == end.line ? column_of(li, end.col) : column_of(li, l->length) + 1;
            gfx_fill_rect(buf, gfx_rect(text_x + a * E.cw, y, (z - a) * E.cw, E.lh),
                          token(IDUNN_COLOR_SELECTION));
        }
        draw_text(buf, l, text_x, y + text_top, token(IDUNN_COLOR_FOREGROUND));
        if (li == E.caret.line && E.focused && E.blink_on)
            gfx_fill_rect(buf, gfx_rect(text_x + column_of(li, E.caret.col) * E.cw, y + px(2),
                                        px(2) > 2 ? px(2) : 2, E.lh - px(4)),
                          token(IDUNN_COLOR_PRIMARY));
    }
    buf->clip = saved;

    // the line numbers, in the muted colour (the caret's line brighter)
    gfx_rect_t gutter = gfx_rect(rect.x, rect.y, gw, rect.h);
    buf->clip = gfx_rect_intersect(saved, gutter);
    gfx_fill_rect(buf, gutter, token(IDUNN_COLOR_BACKGROUND));
    gfx_fill_rect(buf, gfx_rect(rect.x + gw - 1, rect.y, 1, rect.h), token(IDUNN_COLOR_BORDER));
    for (int32_t r = 0; r < rows; r++)
    {
        size_t li = E.top + (size_t)r;
        if (li >= S.buffer.count)
            break;
        char number[24];
        int n = snprintf(number, sizeof(number), "%zu", li + 1);
        int32_t x = rect.x + gw - px(PAD) - n * E.cw;
        gfx_color_t color = token(li == E.caret.line ? IDUNN_COLOR_FOREGROUND
                                                     : IDUNN_COLOR_MUTED_FOREGROUND);
        for (int i = 0; i < n; i++)
            bragi_draw_cell(buf, E.face, x + i * E.cw, rect.y + r * E.lh + text_top,
                            (uint32_t)number[i], color);
    }
    buf->clip = saved;
}

static void resize(idunn_widget_t *canvas, int32_t width, int32_t height, void *data)
{
    (void)canvas;
    (void)data;
    E.width = width;
    E.height = height;
    keep_caret_visible();
}

idunn_widget_t *editor_new(void)
{
    E.scale = theme()->scale;
    E.font_size = saved_font_size();
    E.goal = -1;
    load_face();
    static const idunn_canvas_hooks_t hooks = {draw, input, resize, NULL};
    S.canvas = idunn_canvas(&hooks, NULL);
    idunn_canvas_set_cursor(S.canvas, IDUNN_CURSOR_TEXT);
    return S.canvas;
}
