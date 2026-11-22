/**
 * @file vt.c
 * @brief libvt: the parser, the screens and the scrollback.
 *
 * the parser is a state machine after the DEC one (vt100.net): text,
 * ESC, CSI with its parameters, OSC strings, and the strings it ignores
 * (DCS, APC, PM, SOS). control characters act inside a sequence too.
 *
 * a screen is an array of line pointers, so a scroll moves pointers and
 * not cells. the lines that scroll off the top of the main screen go to
 * the scrollback, a ring of lines.
 */

#include "vt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARAMS_MAX 16
#define OSC_MAX    512
#define PARAM_MAX  9999 // a larger number is cut to this

enum
{
    GROUND,
    ESCAPE,
    ESCAPE_INTER, // ESC and an intermediate: ESC ( 0, ESC # 8
    CSI,
    CSI_IGNORE, // a CSI that is too long or wrong: wait for its final byte
    OSC,
    OSC_ESCAPE, // ESC in an OSC: the start of ST (ESC \)
    STRING,     // DCS, APC, PM, SOS: ignored up to ST
    STRING_ESCAPE,
};

enum
{
    CHARSET_ASCII,
    CHARSET_GRAPHICS, // the DEC special graphics: line drawing
};

typedef struct
{
    int x;
    int y;
    vt_cell_t pen;
    bool origin;
    bool wrap_next;
    int charset[2];
    int shift; // 0: G0, 1: G1
} saved_t;

struct vt_private
{
    vt_line_t **screen; // the one shown: primary or alt
    vt_line_t **primary;
    vt_line_t **alt;

    vt_line_t **ring; // the scrollback
    int ring_size;
    int ring_count;
    int ring_head; // the oldest line

    vt_cell_t pen; // the colours and attributes of new text (ch is not used)
    bool wrap_next; // the cursor is past the last column: the next char wraps
    bool autowrap;
    bool origin; // the cursor moves inside the scroll region
    bool insert;
    int top; // the scroll region, both included
    int bottom;
    bool *tabs;
    int charset[2];
    int shift;
    saved_t saved[2]; // for the primary and the alt screen
    uint32_t last_char; // for REP

    int state;
    int params[PARAMS_MAX];
    bool sub[PARAMS_MAX]; // the parameter came after ':' (a sub-parameter)
    int param_count;
    char marker; // '?', '>', '<' or '=' after CSI, or 0
    char inter;  // an intermediate byte (' ', '!', '(' ...), or 0
    char osc[OSC_MAX];
    int osc_length;
    uint32_t utf8; // the code point read so far
    int utf8_left; // continuation bytes still to come

    vt_reply_t reply;
    void *user;
};

static const vt_cell_t default_pen = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};

static vt_cell_t blank(const vt_t *vt)
{
    // erased cells take the background of the pen, as in xterm
    vt_cell_t c = {0, VT_COLOR_DEFAULT, vt->p->pen.bg, 0};
    return c;
}

static vt_line_t *line_new(int cols, vt_cell_t fill)
{
    vt_line_t *line = malloc(sizeof(vt_line_t) + (size_t)cols * sizeof(vt_cell_t));
    if (!line)
        abort(); // a terminal with lines missing is no use
    line->cols = (uint16_t)cols;
    line->wrapped = false;
    line->dirty = true;
    for (int i = 0; i < cols; i++)
        line->cells[i] = fill;
    return line;
}

static void line_clear(vt_line_t *line, int from, int to, vt_cell_t fill)
{
    if (from < 0)
        from = 0;
    if (to > line->cols)
        to = line->cols;
    for (int i = from; i < to; i++)
        line->cells[i] = fill;
    line->dirty = true;
}

/**
 * the line made wider or narrower: the cells are kept, new ones are empty.
 */
static vt_line_t *line_resize(vt_line_t *line, int cols)
{
    vt_line_t *bigger = line_new(cols, default_pen);
    int keep = line->cols < cols ? line->cols : cols;
    memcpy(bigger->cells, line->cells, (size_t)keep * sizeof(vt_cell_t));
    bigger->wrapped = line->wrapped && line->cols <= cols;
    free(line);
    return bigger;
}

static vt_line_t **screen_new(int cols, int rows)
{
    vt_line_t **lines = malloc((size_t)rows * sizeof(vt_line_t *));
    if (!lines)
        abort();
    for (int i = 0; i < rows; i++)
        lines[i] = line_new(cols, default_pen);
    return lines;
}

static void screen_free(vt_line_t **lines, int rows)
{
    if (!lines)
        return;
    for (int i = 0; i < rows; i++)
        free(lines[i]);
    free(lines);
}

static void all_dirty(vt_t *vt)
{
    for (int y = 0; y < vt->rows; y++)
        vt->p->screen[y]->dirty = true;
}

/**
 * keep a line in the scrollback (it is the ring's now), or free it.
 */
static void push_scrollback(vt_t *vt, vt_line_t *line)
{
    struct vt_private *p = vt->p;
    if (p->ring_size == 0)
    {
        free(line);
        return;
    }
    line->dirty = false;
    if (p->ring_count < p->ring_size)
    {
        p->ring[(p->ring_head + p->ring_count) % p->ring_size] = line;
        p->ring_count++;
        return;
    }
    free(p->ring[p->ring_head]);
    p->ring[p->ring_head] = line;
    p->ring_head = (p->ring_head + 1) % p->ring_size;
}

static void clear_scrollback(vt_t *vt)
{
    struct vt_private *p = vt->p;
    for (int i = 0; i < p->ring_count; i++)
        free(p->ring[(p->ring_head + i) % p->ring_size]);
    p->ring_count = 0;
    p->ring_head = 0;
}

/**
 * lines top to bottom move up by n; empty lines come in at the bottom.
 * @param keep the lines that go off the top go to the scrollback (a line
 *        feed on the main screen with the region at the top).
 */
static void scroll_up(vt_t *vt, int top, int bottom, int n, bool keep)
{
    struct vt_private *p = vt->p;
    int height = bottom - top + 1;
    if (n > height)
        n = height;
    if (n <= 0)
        return;
    keep = keep && top == 0 && p->screen == p->primary;
    for (int i = 0; i < n; i++)
    {
        if (keep)
            push_scrollback(vt, p->screen[top + i]);
        else
            free(p->screen[top + i]);
    }
    memmove(&p->screen[top], &p->screen[top + n], (size_t)(height - n) * sizeof(vt_line_t *));
    for (int i = bottom - n + 1; i <= bottom; i++)
        p->screen[i] = line_new(vt->cols, blank(vt));
    if (top == 0 && bottom == vt->rows - 1)
        vt->scrolled += n; // the app can move the pixels up
    else
        for (int i = top; i <= bottom; i++)
            p->screen[i]->dirty = true;
}

/** lines top to bottom move down by n; empty lines come in at the top */
static void scroll_down(vt_t *vt, int top, int bottom, int n)
{
    struct vt_private *p = vt->p;
    int height = bottom - top + 1;
    if (n > height)
        n = height;
    if (n <= 0)
        return;
    for (int i = bottom - n + 1; i <= bottom; i++)
        free(p->screen[i]);
    memmove(&p->screen[top + n], &p->screen[top], (size_t)(height - n) * sizeof(vt_line_t *));
    for (int i = top; i < top + n; i++)
        p->screen[i] = line_new(vt->cols, blank(vt));
    for (int i = top; i <= bottom; i++)
        p->screen[i]->dirty = true;
}

static void set_tabs(vt_t *vt)
{
    for (int x = 0; x < vt->cols; x++)
        vt->p->tabs[x] = x > 0 && x % 8 == 0;
}

static void reply(vt_t *vt, const char *text)
{
    if (vt->p->reply)
        vt->p->reply(text, strlen(text), vt->p->user);
}

static void reset(vt_t *vt)
{
    struct vt_private *p = vt->p;
    p->pen = default_pen;
    p->wrap_next = false;
    p->autowrap = true;
    p->origin = false;
    p->insert = false;
    p->top = 0;
    p->bottom = vt->rows - 1;
    p->charset[0] = p->charset[1] = CHARSET_ASCII;
    p->shift = 0;
    p->last_char = 0;
    vt->cursor_visible = true;
    vt->cursor_blink = true;
    vt->cursor_shape = VT_CURSOR_BLOCK;
    vt->app_cursor_keys = false;
    vt->app_keypad = false;
    vt->bracketed_paste = false;
    vt->mouse = VT_MOUSE_OFF;
    vt->mouse_sgr = false;
    vt->focus_reports = false;
    vt->reverse_video = false;
    for (int i = 0; i < 2; i++)
    {
        p->saved[i] = (saved_t){0};
        p->saved[i].pen = default_pen;
    }
    set_tabs(vt);
}

vt_t *vt_new(int cols, int rows, int scrollback)
{
    if (cols < 1 || rows < 1 || scrollback < 0)
        return NULL;
    vt_t *vt = calloc(1, sizeof(vt_t));
    struct vt_private *p = calloc(1, sizeof(struct vt_private));
    if (!vt || !p)
    {
        free(vt);
        free(p);
        return NULL;
    }
    vt->p = p;
    vt->cols = cols;
    vt->rows = rows;
    p->primary = screen_new(cols, rows);
    p->alt = screen_new(cols, rows);
    p->screen = p->primary;
    p->ring_size = scrollback;
    p->ring = scrollback ? calloc((size_t)scrollback, sizeof(vt_line_t *)) : NULL;
    p->tabs = calloc((size_t)cols, sizeof(bool));
    if ((scrollback && !p->ring) || !p->tabs)
        abort();
    reset(vt);
    return vt;
}

void vt_free(vt_t *vt)
{
    if (!vt)
        return;
    clear_scrollback(vt);
    free(vt->p->ring);
    screen_free(vt->p->primary, vt->rows);
    screen_free(vt->p->alt, vt->rows);
    free(vt->p->tabs);
    free(vt->p);
    free(vt);
}

void vt_set_reply(vt_t *vt, vt_reply_t fn, void *user)
{
    vt->p->reply = fn;
    vt->p->user = user;
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

/** move the cursor; in origin mode y counts from the region's top */
static void move_to(vt_t *vt, int x, int y)
{
    struct vt_private *p = vt->p;
    int top = p->origin ? p->top : 0;
    int bottom = p->origin ? p->bottom : vt->rows - 1;
    vt->cursor_x = clamp(x, 0, vt->cols - 1);
    vt->cursor_y = clamp(y + top, top, bottom);
    p->wrap_next = false;
}

static void line_feed(vt_t *vt)
{
    struct vt_private *p = vt->p;
    if (vt->cursor_y == p->bottom)
        scroll_up(vt, p->top, p->bottom, 1, true);
    else if (vt->cursor_y < vt->rows - 1)
        vt->cursor_y++;
}

static void reverse_index(vt_t *vt)
{
    struct vt_private *p = vt->p;
    if (vt->cursor_y == p->top)
        scroll_down(vt, p->top, p->bottom, 1);
    else if (vt->cursor_y > 0)
        vt->cursor_y--;
}

static vt_line_t *cursor_line(vt_t *vt)
{
    return vt->p->screen[vt->cursor_y];
}

// the DEC special graphics characters for 0x5F to 0x7E
static const uint16_t dec_graphics[32] = {
    0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B,
    0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C,
    0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

static void put_char(vt_t *vt, uint32_t ch)
{
    struct vt_private *p = vt->p;
    if (p->charset[p->shift] == CHARSET_GRAPHICS && ch >= 0x5F && ch <= 0x7E)
        ch = dec_graphics[ch - 0x5F];
    if (p->wrap_next && p->autowrap)
    {
        cursor_line(vt)->wrapped = true;
        vt->cursor_x = 0;
        line_feed(vt);
    }
    p->wrap_next = false;
    vt_line_t *line = cursor_line(vt);
    int x = vt->cursor_x;
    if (p->insert)
        memmove(&line->cells[x + 1], &line->cells[x],
                (size_t)(vt->cols - x - 1) * sizeof(vt_cell_t));
    vt_cell_t cell = p->pen;
    cell.ch = ch;
    line->cells[x] = cell;
    line->dirty = true;
    p->last_char = ch;
    if (x == vt->cols - 1)
        p->wrap_next = true;
    else
        vt->cursor_x++;
}

static void tab_forward(vt_t *vt, int n)
{
    while (n-- > 0 && vt->cursor_x < vt->cols - 1)
    {
        do
            vt->cursor_x++;
        while (vt->cursor_x < vt->cols - 1 && !vt->p->tabs[vt->cursor_x]);
    }
}

static void tab_back(vt_t *vt, int n)
{
    while (n-- > 0 && vt->cursor_x > 0)
    {
        do
            vt->cursor_x--;
        while (vt->cursor_x > 0 && !vt->p->tabs[vt->cursor_x]);
    }
}

static void save_cursor(vt_t *vt)
{
    struct vt_private *p = vt->p;
    saved_t *s = &p->saved[p->screen == p->alt];
    s->x = vt->cursor_x;
    s->y = vt->cursor_y;
    s->pen = p->pen;
    s->origin = p->origin;
    s->wrap_next = p->wrap_next;
    s->charset[0] = p->charset[0];
    s->charset[1] = p->charset[1];
    s->shift = p->shift;
}

static void restore_cursor(vt_t *vt)
{
    struct vt_private *p = vt->p;
    saved_t *s = &p->saved[p->screen == p->alt];
    vt->cursor_x = clamp(s->x, 0, vt->cols - 1);
    vt->cursor_y = clamp(s->y, 0, vt->rows - 1);
    p->pen = s->pen;
    p->origin = s->origin;
    p->wrap_next = s->wrap_next;
    p->charset[0] = s->charset[0];
    p->charset[1] = s->charset[1];
    p->shift = s->shift;
}

static void use_alt_screen(vt_t *vt, bool on, bool clear)
{
    struct vt_private *p = vt->p;
    if (on == (p->screen == p->alt))
        return;
    p->screen = on ? p->alt : p->primary;
    vt->alt_screen = on;
    if (clear)
        for (int y = 0; y < vt->rows; y++)
            line_clear(p->screen[y], 0, vt->cols, blank(vt));
    vt->scrolled = 0;
    all_dirty(vt);
}

static void control(vt_t *vt, uint8_t c)
{
    struct vt_private *p = vt->p;
    switch (c)
    {
    case 0x07: // BEL
        vt->bell = true;
        break;
    case 0x08: // BS
        if (vt->cursor_x > 0)
            vt->cursor_x--;
        p->wrap_next = false;
        break;
    case 0x09: // HT
        tab_forward(vt, 1);
        break;
    case 0x0A: // LF, VT, FF
    case 0x0B:
    case 0x0C:
        line_feed(vt);
        break;
    case 0x0D: // CR
        vt->cursor_x = 0;
        p->wrap_next = false;
        break;
    case 0x0E: // SO: G1
        p->shift = 1;
        break;
    case 0x0F: // SI: G0
        p->shift = 0;
        break;
    default:
        break;
    }
}

/** parameter i, or def when it is not there or 0 */
static int param(const vt_t *vt, int i, int def)
{
    const struct vt_private *p = vt->p;
    return i < p->param_count && p->params[i] ? p->params[i] : def;
}

static void erase_display(vt_t *vt, int how)
{
    struct vt_private *p = vt->p;
    vt_cell_t fill = blank(vt);
    int y = vt->cursor_y;
    switch (how)
    {
    case 0: // from the cursor to the end
        line_clear(p->screen[y], vt->cursor_x, vt->cols, fill);
        for (int i = y + 1; i < vt->rows; i++)
            line_clear(p->screen[i], 0, vt->cols, fill);
        break;
    case 1: // from the start to the cursor
        for (int i = 0; i < y; i++)
            line_clear(p->screen[i], 0, vt->cols, fill);
        line_clear(p->screen[y], 0, vt->cursor_x + 1, fill);
        break;
    case 2:
        for (int i = 0; i < vt->rows; i++)
        {
            line_clear(p->screen[i], 0, vt->cols, fill);
            p->screen[i]->wrapped = false;
        }
        break;
    case 3: // the scrollback (xterm)
        clear_scrollback(vt);
        break;
    default:
        break;
    }
}

static void erase_line(vt_t *vt, int how)
{
    vt_line_t *line = cursor_line(vt);
    vt_cell_t fill = blank(vt);
    if (how == 0)
        line_clear(line, vt->cursor_x, vt->cols, fill);
    else if (how == 1)
        line_clear(line, 0, vt->cursor_x + 1, fill);
    else if (how == 2)
        line_clear(line, 0, vt->cols, fill);
    if (how != 1)
        line->wrapped = false;
}

static void delete_chars(vt_t *vt, int n)
{
    vt_line_t *line = cursor_line(vt);
    int x = vt->cursor_x;
    n = clamp(n, 1, vt->cols - x);
    memmove(&line->cells[x], &line->cells[x + n], (size_t)(vt->cols - x - n) * sizeof(vt_cell_t));
    line_clear(line, vt->cols - n, vt->cols, blank(vt));
    vt->p->wrap_next = false;
}

static void insert_chars(vt_t *vt, int n)
{
    vt_line_t *line = cursor_line(vt);
    int x = vt->cursor_x;
    n = clamp(n, 1, vt->cols - x);
    memmove(&line->cells[x + n], &line->cells[x], (size_t)(vt->cols - x - n) * sizeof(vt_cell_t));
    line_clear(line, x, x + n, blank(vt));
    vt->p->wrap_next = false;
}

/**
 * a colour from SGR 38 or 48 at parameter i: 5;n or 2;r;g;b, with ';'
 * or ':' between them.
 * @return the parameters it used after i.
 */
static int extended_color(const vt_t *vt, int i, uint32_t *color)
{
    const struct vt_private *p = vt->p;
    if (i + 1 >= p->param_count)
        return 0;
    int kind = p->params[i + 1];
    if (kind == 5 && i + 2 < p->param_count)
    {
        *color = VT_COLOR_INDEX | (uint32_t)(p->params[i + 2] & 0xFF);
        return 2;
    }
    if (kind == 2)
    {
        // 38:2:cs:r:g:b has a colour space before r
        int first = i + 2;
        int subs = 0;
        for (int j = i + 1; j < p->param_count && p->sub[j]; j++)
            subs++;
        if (p->sub[i + 1] && subs >= 5)
            first++;
        if (first + 2 >= p->param_count)
            return p->param_count - i - 1;
        uint32_t r = (uint32_t)(p->params[first] & 0xFF);
        uint32_t g = (uint32_t)(p->params[first + 1] & 0xFF);
        uint32_t b = (uint32_t)(p->params[first + 2] & 0xFF);
        *color = VT_COLOR_RGB | r << 16 | g << 8 | b;
        return first + 2 - i;
    }
    return 1;
}

static void sgr(vt_t *vt)
{
    struct vt_private *p = vt->p;
    vt_cell_t *pen = &p->pen;
    if (p->param_count == 0)
    {
        *pen = default_pen;
        return;
    }
    for (int i = 0; i < p->param_count; i++)
    {
        int n = p->params[i];
        if (p->sub[i])
            continue; // a sub-parameter that nothing used (4:3)
        switch (n)
        {
        case 0:
            *pen = default_pen;
            break;
        case 1:
            pen->attr |= VT_BOLD;
            break;
        case 2:
            pen->attr |= VT_DIM;
            break;
        case 3:
            pen->attr |= VT_ITALIC;
            break;
        case 4:
            // 4:0 is no underline; the other styles are drawn as one line
            if (i + 1 < p->param_count && p->sub[i + 1] && p->params[i + 1] == 0)
                pen->attr &= (uint16_t)~VT_UNDERLINE;
            else
                pen->attr |= VT_UNDERLINE;
            break;
        case 5:
        case 6:
            pen->attr |= VT_BLINK;
            break;
        case 7:
            pen->attr |= VT_INVERSE;
            break;
        case 8:
            pen->attr |= VT_HIDDEN;
            break;
        case 9:
            pen->attr |= VT_STRIKE;
            break;
        case 21:
            pen->attr |= VT_UNDERLINE;
            break;
        case 22:
            pen->attr &= (uint16_t)~(VT_BOLD | VT_DIM);
            break;
        case 23:
            pen->attr &= (uint16_t)~VT_ITALIC;
            break;
        case 24:
            pen->attr &= (uint16_t)~VT_UNDERLINE;
            break;
        case 25:
            pen->attr &= (uint16_t)~VT_BLINK;
            break;
        case 27:
            pen->attr &= (uint16_t)~VT_INVERSE;
            break;
        case 28:
            pen->attr &= (uint16_t)~VT_HIDDEN;
            break;
        case 29:
            pen->attr &= (uint16_t)~VT_STRIKE;
            break;
        case 38:
            i += extended_color(vt, i, &pen->fg);
            break;
        case 39:
            pen->fg = VT_COLOR_DEFAULT;
            break;
        case 48:
            i += extended_color(vt, i, &pen->bg);
            break;
        case 49:
            pen->bg = VT_COLOR_DEFAULT;
            break;
        default:
            if (n >= 30 && n <= 37)
                pen->fg = VT_COLOR_INDEX | (uint32_t)(n - 30);
            else if (n >= 40 && n <= 47)
                pen->bg = VT_COLOR_INDEX | (uint32_t)(n - 40);
            else if (n >= 90 && n <= 97)
                pen->fg = VT_COLOR_INDEX | (uint32_t)(n - 90 + 8);
            else if (n >= 100 && n <= 107)
                pen->bg = VT_COLOR_INDEX | (uint32_t)(n - 100 + 8);
            break;
        }
    }
}

static void set_private_mode(vt_t *vt, int mode, bool on)
{
    struct vt_private *p = vt->p;
    switch (mode)
    {
    case 1:
        vt->app_cursor_keys = on;
        break;
    case 5:
        if (vt->reverse_video != on)
            all_dirty(vt);
        vt->reverse_video = on;
        break;
    case 6:
        p->origin = on;
        move_to(vt, 0, 0);
        break;
    case 7:
        p->autowrap = on;
        if (!on)
            p->wrap_next = false;
        break;
    case 12:
        vt->cursor_blink = on;
        break;
    case 25:
        vt->cursor_visible = on;
        break;
    case 47:
    case 1047:
        if (!on && mode == 1047 && vt->alt_screen)
            for (int y = 0; y < vt->rows; y++)
                line_clear(p->alt[y], 0, vt->cols, default_pen);
        use_alt_screen(vt, on, false);
        break;
    case 1049:
        if (on)
        {
            save_cursor(vt);
            use_alt_screen(vt, true, true);
        }
        else
        {
            use_alt_screen(vt, false, false);
            restore_cursor(vt);
        }
        break;
    case 1000:
        vt->mouse = on ? VT_MOUSE_CLICKS : VT_MOUSE_OFF;
        break;
    case 1002:
        vt->mouse = on ? VT_MOUSE_DRAG : VT_MOUSE_OFF;
        break;
    case 1003:
        vt->mouse = on ? VT_MOUSE_MOTION : VT_MOUSE_OFF;
        break;
    case 1004:
        vt->focus_reports = on;
        break;
    case 1006:
        vt->mouse_sgr = on;
        break;
    case 2004:
        vt->bracketed_paste = on;
        break;
    default:
        break;
    }
}

static void set_scroll_region(vt_t *vt)
{
    struct vt_private *p = vt->p;
    int top = param(vt, 0, 1) - 1;
    int bottom = param(vt, 1, vt->rows) - 1;
    if (bottom > vt->rows - 1)
        bottom = vt->rows - 1;
    if (top >= bottom)
        return;
    p->top = top;
    p->bottom = bottom;
    move_to(vt, 0, 0);
}

static void csi_dispatch(vt_t *vt, uint8_t final)
{
    struct vt_private *p = vt->p;
    char buf[32];

    if (p->marker == '?')
    {
        if (final == 'h' || final == 'l')
            for (int i = 0; i < p->param_count; i++)
                set_private_mode(vt, p->params[i], final == 'h');
        return;
    }
    if (p->marker == '>')
    {
        if (final == 'c')
            reply(vt, "\033[>0;0;0c"); // the secondary device attributes
        return;
    }
    if (p->marker)
        return;
    if (p->inter == ' ' && final == 'q')
    {
        // DECSCUSR: 0 and 1 a blinking block, 2 a block, 3 and 4 underline, 5 and 6 bar
        int shape = param(vt, 0, 1);
        vt->cursor_shape = shape <= 2 ? VT_CURSOR_BLOCK
                         : shape <= 4 ? VT_CURSOR_UNDERLINE
                                      : VT_CURSOR_BAR;
        vt->cursor_blink = shape == 0 || shape % 2 == 1;
        return;
    }
    if (p->inter == '!' && final == 'p')
    {
        // DECSTR, the soft reset: the modes, not the screen
        reset(vt);
        return;
    }
    if (p->inter)
        return;

    int n = param(vt, 0, 1);
    int x = vt->cursor_x;
    int y = vt->cursor_y;
    switch (final)
    {
    case '@':
        insert_chars(vt, n);
        break;
    case 'A':
        vt->cursor_y = clamp(y - n, y >= p->top ? p->top : 0, vt->rows - 1);
        p->wrap_next = false;
        break;
    case 'B':
    case 'e':
        vt->cursor_y = clamp(y + n, 0, y <= p->bottom ? p->bottom : vt->rows - 1);
        p->wrap_next = false;
        break;
    case 'C':
    case 'a':
        vt->cursor_x = clamp(x + n, 0, vt->cols - 1);
        p->wrap_next = false;
        break;
    case 'D':
        vt->cursor_x = clamp(x - n, 0, vt->cols - 1);
        p->wrap_next = false;
        break;
    case 'E':
        vt->cursor_y = clamp(y + n, 0, y <= p->bottom ? p->bottom : vt->rows - 1);
        vt->cursor_x = 0;
        p->wrap_next = false;
        break;
    case 'F':
        vt->cursor_y = clamp(y - n, y >= p->top ? p->top : 0, vt->rows - 1);
        vt->cursor_x = 0;
        p->wrap_next = false;
        break;
    case 'G':
    case '`':
        vt->cursor_x = clamp(n - 1, 0, vt->cols - 1);
        p->wrap_next = false;
        break;
    case 'H':
    case 'f':
        move_to(vt, param(vt, 1, 1) - 1, n - 1);
        break;
    case 'I':
        tab_forward(vt, n);
        break;
    case 'J':
        erase_display(vt, param(vt, 0, 0));
        break;
    case 'K':
        erase_line(vt, param(vt, 0, 0));
        break;
    case 'L':
        if (y >= p->top && y <= p->bottom)
        {
            scroll_down(vt, y, p->bottom, n);
            vt->cursor_x = 0;
        }
        break;
    case 'M':
        if (y >= p->top && y <= p->bottom)
        {
            scroll_up(vt, y, p->bottom, n, false);
            vt->cursor_x = 0;
        }
        break;
    case 'P':
        delete_chars(vt, n);
        break;
    case 'S':
        scroll_up(vt, p->top, p->bottom, n, false);
        break;
    case 'T':
        scroll_down(vt, p->top, p->bottom, n);
        break;
    case 'X':
        line_clear(cursor_line(vt), x, x + n, blank(vt));
        break;
    case 'Z':
        tab_back(vt, n);
        break;
    case 'b':
        // REP: the last character again
        if (p->last_char)
            for (int i = 0; i < n && i < vt->cols * vt->rows; i++)
                put_char(vt, p->last_char);
        break;
    case 'c':
        if (param(vt, 0, 0) == 0)
            reply(vt, "\033[?62;22c"); // a VT220 with ANSI colour
        break;
    case 'd':
        move_to(vt, x, n - 1);
        break;
    case 'g':
        if (param(vt, 0, 0) == 0)
            p->tabs[x] = false;
        else if (param(vt, 0, 0) == 3)
            memset(p->tabs, 0, (size_t)vt->cols * sizeof(bool));
        break;
    case 'h':
    case 'l':
        for (int i = 0; i < p->param_count; i++)
            if (p->params[i] == 4)
                p->insert = final == 'h';
        break;
    case 'm':
        sgr(vt);
        break;
    case 'n':
        if (param(vt, 0, 0) == 5)
            reply(vt, "\033[0n");
        else if (param(vt, 0, 0) == 6)
        {
            int row = y + 1 - (p->origin ? p->top : 0);
            snprintf(buf, sizeof(buf), "\033[%d;%dR", row, x + 1);
            reply(vt, buf);
        }
        break;
    case 'r':
        set_scroll_region(vt);
        break;
    case 's':
        save_cursor(vt);
        break;
    case 'u':
        restore_cursor(vt);
        break;
    default:
        break; // t (window ops) and the rest: not used
    }
}

static void esc_dispatch(vt_t *vt, uint8_t final)
{
    struct vt_private *p = vt->p;
    if (p->inter == '(' || p->inter == ')')
    {
        p->charset[p->inter == ')'] = final == '0' ? CHARSET_GRAPHICS : CHARSET_ASCII;
        return;
    }
    if (p->inter == '#' && final == '8')
    {
        // DECALN: the screen full of E
        for (int y = 0; y < vt->rows; y++)
        {
            vt_cell_t e = default_pen;
            e.ch = 'E';
            line_clear(p->screen[y], 0, vt->cols, e);
        }
        return;
    }
    if (p->inter)
        return;
    switch (final)
    {
    case '7':
        save_cursor(vt);
        break;
    case '8':
        restore_cursor(vt);
        break;
    case 'D':
        line_feed(vt);
        break;
    case 'E':
        vt->cursor_x = 0;
        p->wrap_next = false;
        line_feed(vt);
        break;
    case 'H':
        p->tabs[vt->cursor_x] = true;
        break;
    case 'M':
        reverse_index(vt);
        break;
    case 'c':
        // RIS: all of it
        use_alt_screen(vt, false, false);
        reset(vt);
        clear_scrollback(vt);
        erase_display(vt, 2);
        vt->cursor_x = vt->cursor_y = 0;
        vt->title[0] = '\0';
        vt->title_changed = true;
        break;
    case '=':
        vt->app_keypad = true;
        break;
    case '>':
        vt->app_keypad = false;
        break;
    default:
        break;
    }
}

static void osc_dispatch(vt_t *vt)
{
    struct vt_private *p = vt->p;
    p->osc[p->osc_length] = '\0';
    // "0;title" and "2;title" set the title; "1;" is the icon name
    if ((p->osc[0] == '0' || p->osc[0] == '2') && p->osc[1] == ';')
    {
        snprintf(vt->title, sizeof(vt->title), "%s", p->osc + 2);
        vt->title_changed = true;
    }
}

static void csi_start(vt_t *vt)
{
    struct vt_private *p = vt->p;
    p->state = CSI;
    p->param_count = 0;
    p->params[0] = 0;
    p->sub[0] = false;
    p->marker = 0;
    p->inter = 0;
}

static void csi_byte(vt_t *vt, uint8_t c)
{
    struct vt_private *p = vt->p;
    if (c >= '0' && c <= '9')
    {
        if (p->param_count == 0)
            p->param_count = 1;
        int *v = &p->params[p->param_count - 1];
        *v = *v * 10 + (c - '0');
        if (*v > PARAM_MAX)
            *v = PARAM_MAX;
    }
    else if (c == ';' || c == ':')
    {
        if (p->param_count == 0)
            p->param_count = 1;
        if (p->param_count == PARAMS_MAX)
        {
            p->state = CSI_IGNORE;
            return;
        }
        p->params[p->param_count] = 0;
        p->sub[p->param_count] = c == ':';
        p->param_count++;
    }
    else if (c >= '<' && c <= '?')
    {
        if (p->param_count || p->marker || p->inter)
            p->state = CSI_IGNORE;
        else
            p->marker = (char)c;
    }
    else if (c >= 0x20 && c <= 0x2F)
    {
        if (p->inter)
            p->state = CSI_IGNORE;
        else
            p->inter = (char)c;
    }
    else if (c >= 0x40 && c <= 0x7E)
    {
        p->state = GROUND;
        csi_dispatch(vt, c);
    }
    else
    {
        p->state = CSI_IGNORE;
    }
}

static void ground_byte(vt_t *vt, uint8_t c)
{
    struct vt_private *p = vt->p;
    if (c < 0x80)
    {
        if (p->utf8_left)
        {
            p->utf8_left = 0;
            put_char(vt, 0xFFFD); // a sequence cut short
        }
        if (c >= 0x20 && c != 0x7F)
            put_char(vt, c);
        return;
    }
    if ((c & 0xC0) == 0x80)
    {
        if (!p->utf8_left)
        {
            put_char(vt, 0xFFFD);
            return;
        }
        p->utf8 = p->utf8 << 6 | (c & 0x3F);
        if (--p->utf8_left == 0)
        {
            uint32_t cp = p->utf8;
            bool bad = cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF);
            put_char(vt, bad ? 0xFFFD : cp);
        }
        return;
    }
    if (p->utf8_left)
        put_char(vt, 0xFFFD);
    if ((c & 0xE0) == 0xC0)
    {
        p->utf8 = c & 0x1F;
        p->utf8_left = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        p->utf8 = c & 0x0F;
        p->utf8_left = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        p->utf8 = c & 0x07;
        p->utf8_left = 3;
    }
    else
    {
        p->utf8_left = 0;
        put_char(vt, 0xFFFD);
    }
}

static void byte(vt_t *vt, uint8_t c)
{
    struct vt_private *p = vt->p;

    // the strings take everything up to their end
    if (p->state == OSC || p->state == OSC_ESCAPE)
    {
        if (c == 0x07 || (p->state == OSC_ESCAPE && c == '\\'))
        {
            osc_dispatch(vt);
            p->state = GROUND;
        }
        else if (c == 0x1B)
        {
            p->state = OSC_ESCAPE;
        }
        else if (p->state == OSC_ESCAPE)
        {
            // ESC and something else: the OSC ends, a new sequence starts
            osc_dispatch(vt);
            p->state = ESCAPE;
            p->inter = 0;
            byte(vt, c);
        }
        else if (p->osc_length < OSC_MAX - 1 && c >= 0x20)
        {
            p->osc[p->osc_length++] = (char)c;
        }
        return;
    }
    if (p->state == STRING || p->state == STRING_ESCAPE)
    {
        if (c == 0x07 || (p->state == STRING_ESCAPE && c == '\\'))
            p->state = GROUND;
        else
            p->state = c == 0x1B ? STRING_ESCAPE : STRING;
        return;
    }

    // control characters act in every other state
    if (c == 0x1B)
    {
        p->state = ESCAPE;
        p->inter = 0;
        p->utf8_left = 0;
        return;
    }
    if (c == 0x18 || c == 0x1A) // CAN, SUB: the sequence is dropped
    {
        p->state = GROUND;
        return;
    }
    if (c < 0x20)
    {
        control(vt, c);
        return;
    }

    switch (p->state)
    {
    case GROUND:
        ground_byte(vt, c);
        break;
    case ESCAPE:
        if (c == '[')
            csi_start(vt);
        else if (c == ']')
        {
            p->state = OSC;
            p->osc_length = 0;
        }
        else if (c == 'P' || c == '_' || c == '^' || c == 'X')
            p->state = STRING;
        else if (c >= 0x20 && c <= 0x2F)
        {
            p->inter = (char)c;
            p->state = ESCAPE_INTER;
        }
        else
        {
            p->state = GROUND;
            esc_dispatch(vt, c);
        }
        break;
    case ESCAPE_INTER:
        if (c >= 0x30 && c <= 0x7E)
        {
            p->state = GROUND;
            esc_dispatch(vt, c);
        }
        break;
    case CSI:
        csi_byte(vt, c);
        break;
    case CSI_IGNORE:
        if (c >= 0x40 && c <= 0x7E)
            p->state = GROUND;
        break;
    default:
        p->state = GROUND;
        break;
    }
}

void vt_write(vt_t *vt, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; i++)
        byte(vt, bytes[i]);
}

/** a screen at a new size; the lines above `drop` go to the scrollback */
static vt_line_t **screen_resize(vt_t *vt, vt_line_t **old, int cols, int rows, int drop,
                                 bool keep)
{
    vt_line_t **lines = malloc((size_t)rows * sizeof(vt_line_t *));
    if (!lines)
        abort();
    for (int y = 0; y < drop; y++)
    {
        if (keep)
            push_scrollback(vt, old[y]);
        else
            free(old[y]);
    }
    for (int y = 0; y < rows; y++)
    {
        int from = y + drop;
        if (from < vt->rows)
        {
            lines[y] = old[from]->cols == cols ? old[from] : line_resize(old[from], cols);
            lines[y]->dirty = true;
        }
        else
        {
            lines[y] = line_new(cols, default_pen);
        }
    }
    for (int y = rows + drop; y < vt->rows; y++)
        free(old[y]);
    free(old);
    return lines;
}

void vt_resize(vt_t *vt, int cols, int rows)
{
    struct vt_private *p = vt->p;
    if (cols < 1 || rows < 1 || (cols == vt->cols && rows == vt->rows))
        return;
    bool alt = p->screen == p->alt;
    // the lines above the cursor that no longer fit go up, out of sight
    int drop = vt->cursor_y + 1 > rows ? vt->cursor_y + 1 - rows : 0;
    p->primary = screen_resize(vt, p->primary, cols, rows, alt ? 0 : drop, true);
    p->alt = screen_resize(vt, p->alt, cols, rows, alt ? drop : 0, false);
    p->screen = alt ? p->alt : p->primary;
    for (int i = 0; i < 2; i++)
    {
        saved_t *s = &p->saved[i];
        s->y = clamp(s->y - ((i == 1) == alt ? drop : 0), 0, rows - 1);
        s->x = clamp(s->x, 0, cols - 1);
    }
    vt->cursor_y = clamp(vt->cursor_y - drop, 0, rows - 1);
    vt->cursor_x = clamp(vt->cursor_x, 0, cols - 1);
    bool *tabs = realloc(p->tabs, (size_t)cols * sizeof(bool));
    if (!tabs)
        abort();
    p->tabs = tabs;
    vt->cols = cols;
    vt->rows = rows;
    set_tabs(vt);
    p->top = 0;
    p->bottom = rows - 1;
    p->wrap_next = false;
    vt->scrolled = 0;
}

const vt_line_t *vt_line(const vt_t *vt, int y)
{
    const struct vt_private *p = vt->p;
    if (y >= 0)
        return y < vt->rows ? p->screen[y] : NULL;
    int index = p->ring_count + y;
    if (index < 0)
        return NULL;
    return p->ring[(p->ring_head + index) % p->ring_size];
}

int vt_scrollback_lines(const vt_t *vt)
{
    return vt->p->ring_count;
}

bool vt_line_dirty(const vt_t *vt, int y)
{
    return y >= 0 && y < vt->rows && vt->p->screen[y]->dirty;
}

void vt_clean(vt_t *vt)
{
    for (int y = 0; y < vt->rows; y++)
        vt->p->screen[y]->dirty = false;
    vt->scrolled = 0;
}

static int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static void append(char *out, size_t size, size_t *used, const char *bytes, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (*used + 1 < size)
            out[*used] = bytes[i];
        (*used)++;
    }
}

size_t vt_text(const vt_t *vt, int x0, int y0, int x1, int y1, char *out, size_t size)
{
    size_t used = 0;
    for (int y = y0; y <= y1; y++)
    {
        const vt_line_t *line = vt_line(vt, y);
        if (!line)
            continue;
        int from = y == y0 ? x0 : 0;
        int to = y == y1 ? x1 : line->cols - 1;
        if (to > line->cols - 1)
            to = line->cols - 1;
        bool to_end = to == line->cols - 1;
        // the empty cells at the end of a line are not text
        if (!(to_end && line->wrapped))
            while (to >= from && (line->cells[to].ch == 0 || line->cells[to].ch == ' '))
                to--;
        for (int x = from; x <= to; x++)
        {
            char bytes[4];
            uint32_t ch = line->cells[x].ch ? line->cells[x].ch : ' ';
            append(out, size, &used, bytes, utf8_encode(ch, bytes));
        }
        if (y < y1 && !(to_end && line->wrapped))
            append(out, size, &used, "\n", 1);
    }
    if (size)
        out[used < size ? used : size - 1] = '\0';
    return used;
}

uint32_t vt_palette(int index)
{
    static const uint32_t base[16] = {
        0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };
    index &= 0xFF;
    if (index < 16)
        return base[index];
    if (index < 232)
    {
        static const uint8_t level[6] = {0, 95, 135, 175, 215, 255};
        int i = index - 16;
        return (uint32_t)level[i / 36] << 16 | (uint32_t)level[i / 6 % 6] << 8 | level[i % 6];
    }
    uint32_t grey = (uint32_t)(8 + (index - 232) * 10);
    return grey << 16 | grey << 8 | grey;
}

int vt_key(const vt_t *vt, vt_key_t key, int mods, char *out)
{
    int m = (mods & 7) + 1; // xterm's modifier number: 1 + shift 1, alt 2, ctrl 4
    const char *alt = (mods & VT_MOD_ALT) ? "\033" : "";
    switch (key)
    {
    case VT_KEY_UP:
    case VT_KEY_DOWN:
    case VT_KEY_RIGHT:
    case VT_KEY_LEFT:
    case VT_KEY_HOME:
    case VT_KEY_END:
    {
        static const char finals[] = "ABCDHF";
        char f = finals[key - VT_KEY_UP];
        if (m > 1)
            return sprintf(out, "\033[1;%d%c", m, f);
        return sprintf(out, vt->app_cursor_keys ? "\033O%c" : "\033[%c", f);
    }
    case VT_KEY_INSERT:
    case VT_KEY_DELETE:
    case VT_KEY_PAGE_UP:
    case VT_KEY_PAGE_DOWN:
    {
        static const int codes[] = {2, 3, 5, 6};
        int code = codes[key - VT_KEY_INSERT];
        if (m > 1)
            return sprintf(out, "\033[%d;%d~", code, m);
        return sprintf(out, "\033[%d~", code);
    }
    case VT_KEY_ENTER:
        return sprintf(out, "%s\r", alt);
    case VT_KEY_TAB:
        if (mods & VT_MOD_SHIFT)
            return sprintf(out, "\033[Z");
        return sprintf(out, "%s\t", alt);
    case VT_KEY_BACKSPACE:
        return sprintf(out, "%s%c", alt, (mods & VT_MOD_CTRL) ? 0x08 : 0x7F);
    case VT_KEY_ESCAPE:
        return sprintf(out, "%s\033", alt);
    default:
        break;
    }
    if (key >= VT_KEY_F1 && key <= VT_KEY_F12)
    {
        int f = key - VT_KEY_F1;
        if (f < 4)
        {
            if (m > 1)
                return sprintf(out, "\033[1;%d%c", m, 'P' + f);
            return sprintf(out, "\033O%c", 'P' + f);
        }
        static const int codes[] = {15, 17, 18, 19, 20, 21, 23, 24};
        if (m > 1)
            return sprintf(out, "\033[%d;%d~", codes[f - 4], m);
        return sprintf(out, "\033[%d~", codes[f - 4]);
    }
    return 0;
}

int vt_text_key(const char *text, int mods, char *out)
{
    int n = 0;
    size_t length = strlen(text);
    if (length == 0 || length > 8)
        return 0;
    if (mods & VT_MOD_ALT)
        out[n++] = '\033';
    if ((mods & VT_MOD_CTRL) && length == 1)
    {
        char c = text[0];
        int code = -1;
        if (c >= 'a' && c <= 'z')
            code = c - 'a' + 1;
        else if (c >= '@' && c <= '_')
            code = c - '@'; // A to Z, [ \ ] ^ _ and @
        else if (c == ' ' || c == '2')
            code = 0;
        else if (c == '3')
            code = 0x1B;
        else if (c == '4')
            code = 0x1C;
        else if (c == '5')
            code = 0x1D;
        else if (c == '6')
            code = 0x1E;
        else if (c == '7' || c == '-' || c == '/')
            code = 0x1F;
        else if (c == '8' || c == '?')
            code = 0x7F;
        if (code >= 0)
        {
            out[n++] = (char)code;
            return n;
        }
    }
    memcpy(out + n, text, length);
    return n + (int)length;
}

int vt_mouse(const vt_t *vt, vt_mouse_t kind, int button, int x, int y, int mods, char *out)
{
    if (vt->mouse == VT_MOUSE_OFF)
        return 0;
    if (kind == VT_MOUSE_MOVE && !(vt->mouse == VT_MOUSE_MOTION ||
                                   (vt->mouse == VT_MOUSE_DRAG && button >= 0)))
        return 0;
    int code;
    switch (kind)
    {
    case VT_MOUSE_WHEEL_UP:
        code = 64;
        break;
    case VT_MOUSE_WHEEL_DOWN:
        code = 65;
        break;
    case VT_MOUSE_MOVE:
        code = (button >= 0 ? button : 3) + 32;
        break;
    case VT_MOUSE_RELEASE:
        code = vt->mouse_sgr ? button : 3;
        break;
    default:
        code = button;
        break;
    }
    if (code < 0)
        return 0;
    if (mods & VT_MOD_SHIFT)
        code += 4;
    if (mods & VT_MOD_ALT)
        code += 8;
    if (mods & VT_MOD_CTRL)
        code += 16;
    if (vt->mouse_sgr)
        return sprintf(out, "\033[<%d;%d;%d%c", code, x + 1, y + 1,
                       kind == VT_MOUSE_RELEASE ? 'm' : 'M');
    // the old form: one byte each, so the cell is at most 223
    x = clamp(x + 1, 1, 223);
    y = clamp(y + 1, 1, 223);
    return sprintf(out, "\033[M%c%c%c", 32 + code, 32 + x, 32 + y);
}

const char *vt_paste_begin(const vt_t *vt)
{
    return vt->bracketed_paste ? "\033[200~" : "";
}

const char *vt_paste_end(const vt_t *vt)
{
    return vt->bracketed_paste ? "\033[201~" : "";
}
