/**
 * @file mimir.c
 * @brief mimir, the terminal: the shell in a window.
 *
 * usage: mimir [program [arguments]]   (the shell, /bin/sh, without one)
 *
 * mimir opens a pty, starts the program on its slave side, and shows
 * what it writes: libvt keeps the screen, a canvas draws it in
 * JetBrains Mono. keys go to the program as a terminal sends them.
 *
 *   Ctrl+Shift+C, Ctrl+Shift+V    copy the selection, paste
 *   Shift+Page Up, Shift+Page Down the scrollback (the wheel too)
 *   Ctrl+plus, Ctrl+minus, Ctrl+0 a larger, a smaller, the normal text
 *
 * the normal text size is [terminal] font_size in mimir.conf (settings
 * sets it; open windows follow it).
 *
 * drag to select; a double click selects a word, a triple click a line.
 * a program that asks for the mouse gets it; Shift lets mimir select.
 */

#include <bragi.h>
#include <conf.h>
#include <errno.h>
#include <fcntl.h>
#include <idunn.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vt.h>

#define FONTS      "/usr/share/fonts/jetbrains-mono/JetBrainsMono-"
#define SHELL      "/bin/sh"
#define FONT_SIZE  13 // design pixels
#define FONT_MIN   8
#define FONT_MAX   32
#define PAD        6 // design pixels around the cells
#define SCROLLBACK 10000
#define WHEEL_LINES 3
#define BLINK_MS   600
#define READS_MAX  8 // reads of the pty before the next frame

enum
{
    FACE_REGULAR,
    FACE_BOLD,
    FACE_ITALIC,
    FACE_COUNT
};

static struct
{
    idunn_app_t *app;
    idunn_window_t *win;
    idunn_widget_t *canvas;
    vt_t *vt;
    int master;
    pid_t child;

    bragi_font_t *fonts[FACE_COUNT];
    bragi_face_t *faces[FACE_COUNT];
    int font_size; // design pixels
    int32_t scale;
    int32_t cw; // a cell, in pixels
    int32_t ch;
    int32_t pad;
    int32_t width; // the canvas
    int32_t height;

    int view;          // lines scrolled back into the scrollback (0: the screen)
    bool focused;
    bool blink_on;     // the blinking cursor is shown now
    int cursor_x;      // where the cursor was drawn (-1: nowhere)
    int cursor_y;
    int32_t pointer_x; // the pointer over the canvas, for the wheel
    int32_t pointer_y;

    bool selected; // a selection is shown
    bool selecting; // the button is down
    int sel_unit;   // 1 characters, 2 words, 3 lines
    int anchor_x, anchor_y; // in vt_line rows (the scrollback is negative)
    int head_x, head_y;
} M;

// a calmer palette than xterm's for the 16 colours, for each theme mode
static const uint32_t palette_dark[16] = {
    0x3F3F46, 0xF87171, 0x4ADE80, 0xFACC15, 0x60A5FA, 0xC084FC, 0x22D3EE, 0xD4D4D8,
    0x71717A, 0xFCA5A5, 0x86EFAC, 0xFDE047, 0x93C5FD, 0xD8B4FE, 0x67E8F9, 0xFAFAFA,
};
static const uint32_t palette_light[16] = {
    0x18181B, 0xDC2626, 0x16A34A, 0xA16207, 0x2563EB, 0x9333EA, 0x0891B2, 0xA1A1AA,
    0x52525B, 0xEF4444, 0x22C55E, 0xCA8A04, 0x3B82F6, 0xA855F7, 0x06B6D4, 0x71717A,
};

// the character of a key by its place on a US keyboard, for Ctrl and Alt
// (heimdalld gives no text with Ctrl)
static const char key_chars[KEY_SPACE + 1] = {
    [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4', [KEY_5] = '5',
    [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9', [KEY_0] = '0',
    [KEY_MINUS] = '-', [KEY_EQUAL] = '=', [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e',
    [KEY_R] = 'r', [KEY_T] = 't', [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i',
    [KEY_O] = 'o', [KEY_P] = 'p', [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']',
    [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f', [KEY_G] = 'g',
    [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l', [KEY_SEMICOLON] = ';',
    [KEY_APOSTROPHE] = '\'', [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '\\', [KEY_Z] = 'z',
    [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v', [KEY_B] = 'b', [KEY_N] = 'n',
    [KEY_M] = 'm', [KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/', [KEY_SPACE] = ' ',
};

static const idunn_theme_t *theme(void)
{
    return idunn_app_theme(M.app);
}

static gfx_color_t token(idunn_color_t c)
{
    const idunn_theme_t *t = theme();
    return t->colors[t->mode][c];
}

static gfx_color_t from_rgb(uint32_t c)
{
    return gfx_rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

/**
 * the colour of a cell's text or background.
 */
static gfx_color_t resolve(uint32_t color, bool text)
{
    switch (VT_COLOR_KIND(color))
    {
    case VT_COLOR_INDEX:
    {
        int i = (int)(color & 0xFF);
        if (i < 16)
            return from_rgb(theme()->mode == IDUNN_DARK ? palette_dark[i] : palette_light[i]);
        return from_rgb(vt_palette(i));
    }
    case VT_COLOR_RGB:
        return from_rgb(color & 0xFFFFFF);
    default:
        return token(text ? IDUNN_COLOR_FOREGROUND : IDUNN_COLOR_BACKGROUND);
    }
}

static gfx_color_t mix(gfx_color_t a, gfx_color_t b)
{
    // half of each, channel by channel (both opaque)
    return (((a >> 1) & 0x7F7F7F7Fu) + ((b >> 1) & 0x7F7F7F7Fu)) | 0xFF000000u;
}

static void cell_colors(const vt_cell_t *cell, gfx_color_t *fg, gfx_color_t *bg)
{
    *fg = resolve(cell->fg, true);
    *bg = resolve(cell->bg, false);
    if (((cell->attr & VT_INVERSE) != 0) != M.vt->reverse_video)
    {
        gfx_color_t t = *fg;
        *fg = *bg;
        *bg = t;
    }
    if (cell->attr & VT_DIM)
        *fg = mix(*fg, *bg);
    if (cell->attr & VT_HIDDEN)
        *fg = *bg;
}

static bragi_face_t *face_for(uint16_t attr)
{
    if ((attr & VT_BOLD) && M.faces[FACE_BOLD])
        return M.faces[FACE_BOLD];
    if ((attr & VT_ITALIC) && M.faces[FACE_ITALIC])
        return M.faces[FACE_ITALIC];
    return M.faces[FACE_REGULAR];
}

/** the selection in order: (x0, y0) first; false if there is none */
static bool selection(int *x0, int *y0, int *x1, int *y1)
{
    if (!M.selected)
        return false;
    bool forward = M.anchor_y < M.head_y || (M.anchor_y == M.head_y && M.anchor_x <= M.head_x);
    *x0 = forward ? M.anchor_x : M.head_x;
    *y0 = forward ? M.anchor_y : M.head_y;
    *x1 = forward ? M.head_x : M.anchor_x;
    *y1 = forward ? M.head_y : M.anchor_y;
    return true;
}

static bool is_selected(int x, int y)
{
    int x0, y0, x1, y1;
    if (!selection(&x0, &y0, &x1, &y1))
        return false;
    if (y < y0 || y > y1)
        return false;
    if (y == y0 && x < x0)
        return false;
    if (y == y1 && x > x1)
        return false;
    return true;
}

static gfx_rect_t cell_rect(gfx_rect_t area, int x, int row)
{
    return gfx_rect(area.x + M.pad + x * M.cw, area.y + M.pad + row * M.ch, M.cw, M.ch);
}

/**
 * one row of the canvas: line y of libvt (the scrollback is negative).
 */
static void draw_row(gfx_buffer_t *buf, gfx_rect_t area, int row, int y)
{
    const vt_line_t *line = vt_line(M.vt, y);
    int cols = M.vt->cols;
    gfx_color_t sel = token(IDUNN_COLOR_SELECTION);
    // the backgrounds first, in runs of one colour
    for (int x = 0; x < cols;)
    {
        gfx_color_t fg, bg;
        vt_cell_t empty = {0, VT_COLOR_DEFAULT, VT_COLOR_DEFAULT, 0};
        cell_colors(line && x < line->cols ? &line->cells[x] : &empty, &fg, &bg);
        int end = x + 1;
        while (end < cols)
        {
            gfx_color_t f2, b2;
            cell_colors(line && end < line->cols ? &line->cells[end] : &empty, &f2, &b2);
            if (b2 != bg)
                break;
            end++;
        }
        gfx_rect_t r = cell_rect(area, x, row);
        r.w = (end - x) * M.cw;
        gfx_fill_rect(buf, r, bg);
        x = end;
    }
    for (int x = 0; x < cols; x++)
        if (is_selected(x, y))
            gfx_fill_rect(buf, cell_rect(area, x, row), sel);
    if (!line)
        return;
    for (int x = 0; x < cols && x < line->cols; x++)
    {
        const vt_cell_t *cell = &line->cells[x];
        gfx_color_t fg, bg;
        cell_colors(cell, &fg, &bg);
        gfx_rect_t r = cell_rect(area, x, row);
        if (cell->ch && cell->ch != ' ' && !(cell->attr & VT_HIDDEN))
            bragi_draw_cell(buf, face_for(cell->attr), r.x, r.y, cell->ch, fg);
        int32_t thick = M.scale;
        if (cell->attr & VT_UNDERLINE)
            gfx_fill_rect(buf, gfx_rect(r.x, r.y + M.ch - 2 * thick, M.cw, thick), fg);
        if (cell->attr & VT_STRIKE)
            gfx_fill_rect(buf, gfx_rect(r.x, r.y + M.ch / 2, M.cw, thick), fg);
    }
}

static void draw_cursor(gfx_buffer_t *buf, gfx_rect_t area)
{
    vt_t *vt = M.vt;
    if (M.view != 0 || !vt->cursor_visible)
        return;
    if (M.focused && vt->cursor_blink && !M.blink_on)
        return;
    gfx_rect_t r = cell_rect(area, vt->cursor_x, vt->cursor_y);
    gfx_color_t color = token(IDUNN_COLOR_FOREGROUND);
    int32_t thick = 2 * M.scale;
    if (!M.focused)
    {
        // a box outline: the keyboard is elsewhere
        int32_t t = M.scale;
        gfx_fill_rect(buf, gfx_rect(r.x, r.y, r.w, t), color);
        gfx_fill_rect(buf, gfx_rect(r.x, r.y + r.h - t, r.w, t), color);
        gfx_fill_rect(buf, gfx_rect(r.x, r.y, t, r.h), color);
        gfx_fill_rect(buf, gfx_rect(r.x + r.w - t, r.y, t, r.h), color);
        return;
    }
    switch (vt->cursor_shape)
    {
    case VT_CURSOR_UNDERLINE:
        gfx_fill_rect(buf, gfx_rect(r.x, r.y + r.h - thick, r.w, thick), color);
        break;
    case VT_CURSOR_BAR:
        gfx_fill_rect(buf, gfx_rect(r.x, r.y, thick, r.h), color);
        break;
    default:
    {
        // a block: the character in the background colour inside it
        gfx_fill_rect(buf, r, color);
        const vt_line_t *line = vt_line(vt, vt->cursor_y);
        const vt_cell_t *cell = line && vt->cursor_x < line->cols ? &line->cells[vt->cursor_x]
                                                                  : NULL;
        if (cell && cell->ch && cell->ch != ' ')
            bragi_draw_cell(buf, face_for(cell->attr), r.x, r.y, cell->ch,
                            token(IDUNN_COLOR_BACKGROUND));
        break;
    }
    }
}

static void canvas_draw(idunn_widget_t *canvas, gfx_buffer_t *buf, gfx_rect_t area, void *data)
{
    (void)canvas;
    (void)data;
    gfx_rect_t clip = buf->clip;
    gfx_fill_rect(buf, clip, token(IDUNN_COLOR_BACKGROUND));
    int first = (clip.y - area.y - M.pad) / M.ch;
    int last = (clip.y + clip.h - 1 - area.y - M.pad) / M.ch;
    if (first < 0)
        first = 0;
    if (last > M.vt->rows - 1)
        last = M.vt->rows - 1;
    for (int row = first; row <= last; row++)
        draw_row(buf, area, row, row - M.view);
    draw_cursor(buf, area);
}

/** draw rows again (canvas rows) */
static void damage_rows(int first, int count)
{
    if (count <= 0)
        return;
    idunn_canvas_damage(M.canvas, gfx_rect(0, M.pad + first * M.ch, M.width, count * M.ch));
}

static void damage_all(void)
{
    idunn_canvas_damage(M.canvas, gfx_rect(0, 0, M.width, M.height));
}

static void damage_cursor(void)
{
    if (M.cursor_y >= 0)
        damage_rows(M.cursor_y, 1);
    damage_rows(M.vt->cursor_y, 1);
    M.cursor_x = M.vt->cursor_x;
    M.cursor_y = M.vt->cursor_y;
}

static void clear_selection(void)
{
    if (M.selected)
        damage_all();
    M.selected = false;
}

/**
 * before a frame: move the pixels of a scroll, and draw again what
 * changed since the last frame.
 */
static void canvas_frame(idunn_widget_t *canvas, void *data)
{
    (void)data;
    vt_t *vt = M.vt;
    if (vt->title_changed)
    {
        vt->title_changed = false;
        idunn_window_set_title(M.win, vt->title[0] ? vt->title : "mimir");
    }
    vt->bell = false;
    int scrolled = vt->scrolled;
    if (M.selected && scrolled && !vt->alt_screen)
    {
        // the selection moves with its text
        M.anchor_y -= scrolled;
        M.head_y -= scrolled;
    }
    if (M.view > 0)
    {
        // the scrollback stays where it is while the program writes
        bool changed = scrolled != 0;
        if (scrolled && !vt->alt_screen)
        {
            M.view += scrolled;
            if (M.view > vt_scrollback_lines(vt))
                M.view = vt_scrollback_lines(vt);
        }
        for (int y = 0; y < vt->rows && !changed; y++)
            changed = vt_line_dirty(vt, y);
        if (changed)
            damage_all();
        vt_clean(vt);
        return;
    }
    if (scrolled > 0 && scrolled < vt->rows)
    {
        idunn_canvas_move(canvas, gfx_rect(0, M.pad, M.width, vt->rows * M.ch), -scrolled * M.ch);
        if (M.cursor_y >= 0)
            M.cursor_y -= scrolled; // the old cursor moved with its pixels
    }
    else if (scrolled >= vt->rows)
    {
        damage_all();
    }
    for (int y = 0; y < vt->rows; y++)
    {
        if (!vt_line_dirty(vt, y))
            continue;
        int first = y;
        while (y + 1 < vt->rows && vt_line_dirty(vt, y + 1))
            y++;
        damage_rows(first, y - first + 1);
        // text written over the selection ends it
        for (int r = first; r <= y && M.selected; r++)
        {
            int x0, y0, x1, y1;
            selection(&x0, &y0, &x1, &y1);
            if (r >= y0 && r <= y1)
                clear_selection();
        }
    }
    if (M.cursor_y != vt->cursor_y || M.cursor_x != vt->cursor_x)
        M.blink_on = true; // the cursor shows while it moves
    damage_cursor();
    vt_clean(vt);
}

/**
 * all of data to the program. the pty takes only so much at a time;
 * wait for room, but not for ever.
 */
static void send(const char *data, size_t length)
{
    int waits = 0;
    while (length > 0)
    {
        ssize_t n = write(M.master, data, length);
        if (n > 0)
        {
            data += n;
            length -= (size_t)n;
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR)
            return;
        if (++waits > 200)
            return; // the program does not read: drop the rest
        struct pollfd p = {M.master, POLLOUT, 0};
        poll(&p, 1, 10);
    }
}

static void on_reply(const char *data, size_t length, void *user)
{
    (void)user;
    send(data, length);
}

static void back_to_screen(void)
{
    if (M.view)
    {
        M.view = 0;
        damage_all();
    }
}

/**
 * the size of the text changed: new faces, a new cell.
 */
static bool load_faces(void)
{
    static const char *const names[FACE_COUNT] = {"Regular", "Bold", "Italic"};
    for (int i = 0; i < FACE_COUNT; i++)
    {
        if (!M.fonts[i])
        {
            char path[128];
            snprintf(path, sizeof(path), FONTS "%s.ttf", names[i]);
            M.fonts[i] = bragi_font_open(path);
        }
        bragi_face_free(M.faces[i]);
        M.faces[i] = M.fonts[i] ? bragi_face_new(M.fonts[i], (float)(M.font_size * M.scale))
                                : NULL;
    }
    if (!M.faces[FACE_REGULAR])
        return false;
    bragi_cell_size(M.faces[FACE_REGULAR], &M.cw, &M.ch);
    M.pad = PAD * M.scale;
    return true;
}

/**
 * the grid for the canvas's size; the program hears of it (SIGWINCH).
 */
static void fit_grid(void)
{
    if (M.width <= 0 || M.height <= 0)
        return;
    int cols = (M.width - 2 * M.pad) / M.cw;
    int rows = (M.height - 2 * M.pad) / M.ch;
    cols = cols < 2 ? 2 : cols;
    rows = rows < 1 ? 1 : rows;
    if (cols != M.vt->cols || rows != M.vt->rows)
    {
        vt_resize(M.vt, cols, rows);
        clear_selection();
        if (M.view > vt_scrollback_lines(M.vt))
            M.view = vt_scrollback_lines(M.vt);
    }
    struct winsize size = {(unsigned short)rows, (unsigned short)cols,
                           (unsigned short)M.width, (unsigned short)M.height};
    ioctl(M.master, TIOCSWINSZ, &size);
    M.cursor_y = -1;
    damage_all();
}

static void canvas_resize(idunn_widget_t *canvas, int32_t width, int32_t height, void *data)
{
    (void)canvas;
    (void)data;
    M.width = width;
    M.height = height;
    fit_grid();
}

/** the text size of the settings ([terminal] font_size), or FONT_SIZE */
static int saved_font_size(void)
{
    conf_t *conf = conf_open("mimir");
    long size = conf ? conf_get_int(conf, "terminal", "font_size", FONT_SIZE) : FONT_SIZE;
    conf_free(conf);
    return size < FONT_MIN ? FONT_MIN : size > FONT_MAX ? FONT_MAX : (int)size;
}

static void set_font_size(int size)
{
    if (size < FONT_MIN || size > FONT_MAX || size == M.font_size)
        return;
    M.font_size = size;
    load_faces();
    fit_grid();
}

/** the cell under a point of the canvas, as a vt_line row */
static void cell_at(int32_t px, int32_t py, int *x, int *y)
{
    int cx = (px - M.pad) / M.cw;
    int cy = (py - M.pad) / M.ch;
    if (px < M.pad)
        cx = 0;
    if (py < M.pad)
        cy = 0;
    *x = cx < 0 ? 0 : cx >= M.vt->cols ? M.vt->cols - 1 : cx;
    cy = cy < 0 ? 0 : cy >= M.vt->rows ? M.vt->rows - 1 : cy;
    *y = cy - M.view;
}

static bool word_char(uint32_t ch)
{
    return ch && !strchr(" \t'\"()[]{}<>|;,`", (int)(ch < 128 ? ch : 'a'));
}

/** move the ends of the selection to whole words or lines */
static void snap_selection(void)
{
    int x0, y0, x1, y1;
    if (!selection(&x0, &y0, &x1, &y1) || M.sel_unit == 1)
        return;
    if (M.sel_unit == 3)
    {
        x0 = 0;
        x1 = M.vt->cols - 1;
    }
    else
    {
        const vt_line_t *a = vt_line(M.vt, y0);
        const vt_line_t *b = vt_line(M.vt, y1);
        if (a && x0 < a->cols && word_char(a->cells[x0].ch))
            while (x0 > 0 && word_char(a->cells[x0 - 1].ch))
                x0--;
        if (b && x1 < b->cols && word_char(b->cells[x1].ch))
            while (x1 + 1 < b->cols && word_char(b->cells[x1 + 1].ch))
                x1++;
    }
    bool forward = M.anchor_y < M.head_y || (M.anchor_y == M.head_y && M.anchor_x <= M.head_x);
    M.anchor_x = forward ? x0 : x1;
    M.anchor_y = forward ? y0 : y1;
    M.head_x = forward ? x1 : x0;
    M.head_y = forward ? y1 : y0;
}

static void copy_selection(void)
{
    int x0, y0, x1, y1;
    if (!selection(&x0, &y0, &x1, &y1))
        return;
    size_t size = vt_text(M.vt, x0, y0, x1, y1, NULL, 0) + 1;
    char *text = malloc(size);
    if (!text)
        return;
    vt_text(M.vt, x0, y0, x1, y1, text, size);
    idunn_canvas_copy(M.canvas, text, strlen(text));
    free(text);
}

static void paste(const char *text, size_t length)
{
    const char *begin = vt_paste_begin(M.vt);
    send(begin, strlen(begin));
    // a terminal sends Enter as CR
    size_t start = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (text[i] != '\n')
            continue;
        send(text + start, i - start);
        send("\r", 1);
        start = i + 1;
    }
    send(text + start, length - start);
    const char *end = vt_paste_end(M.vt);
    send(end, strlen(end));
}

static int vt_mods(uint32_t mods)
{
    return ((mods & IDUNN_MOD_SHIFT) ? VT_MOD_SHIFT : 0) |
           ((mods & IDUNN_MOD_ALT) ? VT_MOD_ALT : 0) | ((mods & IDUNN_MOD_CTRL) ? VT_MOD_CTRL : 0);
}

/** the vt key of a key code, or 0 for a key that types text */
static vt_key_t special_key(uint16_t code)
{
    switch (code)
    {
    case KEY_UP:
        return VT_KEY_UP;
    case KEY_DOWN:
        return VT_KEY_DOWN;
    case KEY_LEFT:
        return VT_KEY_LEFT;
    case KEY_RIGHT:
        return VT_KEY_RIGHT;
    case KEY_HOME:
        return VT_KEY_HOME;
    case KEY_END:
        return VT_KEY_END;
    case KEY_INSERT:
        return VT_KEY_INSERT;
    case KEY_DELETE:
        return VT_KEY_DELETE;
    case KEY_PAGEUP:
        return VT_KEY_PAGE_UP;
    case KEY_PAGEDOWN:
        return VT_KEY_PAGE_DOWN;
    case KEY_ENTER:
    case KEY_KPENTER:
        return VT_KEY_ENTER;
    case KEY_TAB:
        return VT_KEY_TAB;
    case KEY_BACKSPACE:
        return VT_KEY_BACKSPACE;
    case KEY_ESC:
        return VT_KEY_ESCAPE;
    case KEY_F11:
        return VT_KEY_F1 + 10;
    case KEY_F12:
        return VT_KEY_F12;
    default:
        if (code >= KEY_F1 && code <= KEY_F10)
            return (vt_key_t)(VT_KEY_F1 + (code - KEY_F1));
        return 0;
    }
}

static void scroll_view(int lines)
{
    int view = M.view + lines;
    int most = vt_scrollback_lines(M.vt);
    view = view < 0 ? 0 : view > most ? most : view;
    if (view != M.view)
    {
        M.view = view;
        damage_all();
    }
}

/** keys mimir uses itself; true if it used it */
static bool own_key(uint16_t code, uint32_t mods)
{
    bool ctrl = mods & IDUNN_MOD_CTRL;
    bool shift = mods & IDUNN_MOD_SHIFT;
    if (ctrl && shift && code == KEY_C)
    {
        copy_selection();
        return true;
    }
    if (ctrl && shift && code == KEY_V)
    {
        idunn_canvas_paste(M.canvas);
        return true;
    }
    if (shift && !ctrl && (code == KEY_PAGEUP || code == KEY_PAGEDOWN) && !M.vt->alt_screen)
    {
        int page = M.vt->rows - 1;
        scroll_view(code == KEY_PAGEUP ? page : -page);
        return true;
    }
    if (ctrl && !shift && (code == KEY_EQUAL || code == KEY_KPPLUS))
    {
        set_font_size(M.font_size + 1);
        return true;
    }
    if (ctrl && !shift && (code == KEY_MINUS || code == KEY_KPMINUS))
    {
        set_font_size(M.font_size - 1);
        return true;
    }
    if (ctrl && !shift && code == KEY_0)
    {
        set_font_size(saved_font_size());
        return true;
    }
    return false;
}

static void key(uint16_t code, uint32_t mods, const char *text)
{
    if (own_key(code, mods))
        return;
    char out[32];
    int n = 0;
    vt_key_t special = special_key(code);
    if (special)
        n = vt_key(M.vt, special, vt_mods(mods), out);
    else if (text && text[0])
        n = vt_text_key(text, 0, out); // the layout made the text, Alt included
    else if ((mods & (IDUNN_MOD_CTRL | IDUNN_MOD_ALT)) && code <= KEY_SPACE && key_chars[code])
    {
        char c[2] = {key_chars[code], '\0'};
        n = vt_text_key(c, vt_mods(mods), out);
    }
    if (n <= 0)
        return;
    send(out, (size_t)n);
    back_to_screen();
    M.blink_on = true;
    damage_cursor();
}

static bool mouse_wanted(uint32_t mods)
{
    return M.vt->mouse != VT_MOUSE_OFF && !(mods & IDUNN_MOD_SHIFT);
}

static void report(vt_mouse_t kind, int button, int32_t px, int32_t py, uint32_t mods)
{
    int x, y;
    cell_at(px, py, &x, &y);
    char out[32];
    int n = vt_mouse(M.vt, kind, button, x, y + M.view, vt_mods(mods), out);
    if (n > 0)
        send(out, (size_t)n);
}

static void press(const idunn_input_t *in)
{
    if (mouse_wanted(in->mods))
    {
        report(VT_MOUSE_PRESS, 0, in->x, in->y, in->mods);
        return;
    }
    int x, y;
    cell_at(in->x, in->y, &x, &y);
    if ((in->mods & IDUNN_MOD_SHIFT) && M.selected)
    {
        M.head_x = x; // Shift+click makes the selection longer
        M.head_y = y;
    }
    else
    {
        M.anchor_x = M.head_x = x;
        M.anchor_y = M.head_y = y;
        M.sel_unit = in->clicks < 1 ? 1 : in->clicks > 3 ? 3 : in->clicks;
    }
    M.selected = M.sel_unit > 1 || (in->mods & IDUNN_MOD_SHIFT);
    M.selecting = true;
    snap_selection();
    damage_all();
}

static void drag(const idunn_input_t *in)
{
    M.pointer_x = in->x;
    M.pointer_y = in->y;
    if (!M.selecting)
    {
        if (mouse_wanted(in->mods))
            report(VT_MOUSE_MOVE, 0, in->x, in->y, in->mods);
        return;
    }
    int x, y;
    cell_at(in->x, in->y, &x, &y);
    if (x == M.head_x && y == M.head_y && M.selected)
        return;
    M.head_x = x;
    M.head_y = y;
    M.selected = true;
    snap_selection();
    damage_all();
}

static void release(const idunn_input_t *in)
{
    if (!M.selecting)
    {
        if (mouse_wanted(in->mods))
            report(VT_MOUSE_RELEASE, 0, in->x, in->y, in->mods);
        return;
    }
    M.selecting = false;
}

static void wheel(int32_t steps)
{
    if (M.vt->mouse != VT_MOUSE_OFF)
    {
        for (int i = 0; i < (steps < 0 ? -steps : steps); i++)
            report(steps > 0 ? VT_MOUSE_WHEEL_UP : VT_MOUSE_WHEEL_DOWN, -1, M.pointer_x,
                   M.pointer_y, 0);
        return;
    }
    if (M.vt->alt_screen)
    {
        // a full-screen program without the mouse: the arrows, as xterm does
        char out[16];
        int n = vt_key(M.vt, steps > 0 ? VT_KEY_UP : VT_KEY_DOWN, 0, out);
        for (int i = 0; i < (steps < 0 ? -steps : steps) * WHEEL_LINES; i++)
            send(out, (size_t)n);
        return;
    }
    scroll_view(steps * WHEEL_LINES);
}

static bool canvas_input(idunn_widget_t *canvas, const idunn_input_t *in, void *data)
{
    (void)data;
    switch (in->type)
    {
    case IDUNN_INPUT_KEY:
        key(in->code, in->mods, in->text);
        return true;
    case IDUNN_INPUT_PASTE:
        back_to_screen();
        paste(in->text, in->length);
        return true;
    case IDUNN_INPUT_PRESS:
        press(in);
        return true;
    case IDUNN_INPUT_DRAG:
    case IDUNN_INPUT_MOTION:
        drag(in);
        return true;
    case IDUNN_INPUT_RELEASE:
        release(in);
        return true;
    case IDUNN_INPUT_SCROLL:
        wheel(in->steps);
        return true;
    case IDUNN_INPUT_FOCUS:
    case IDUNN_INPUT_BLUR:
        M.focused = in->type == IDUNN_INPUT_FOCUS;
        M.blink_on = true;
        if (M.vt->focus_reports)
            send(M.focused ? "\033[I" : "\033[O", 3);
        if (M.focused)
            idunn_canvas_wake(canvas, BLINK_MS);
        damage_cursor();
        return true;
    case IDUNN_INPUT_TIMER:
        if (!M.focused)
            return true;
        M.blink_on = !M.blink_on;
        if (M.vt->cursor_blink && M.vt->cursor_visible)
            damage_cursor();
        idunn_canvas_wake(canvas, BLINK_MS);
        return true;
    default:
        return false;
    }
}

/**
 * events for no window: the settings of mimir changed (the text size).
 */
static void on_event(idunn_app_t *app, const heim_msg_t *msg, void *data)
{
    (void)app;
    (void)data;
    if (msg->type == HEIM_EV_SETTINGS && strcmp(msg->settings.name, "mimir") == 0)
        set_font_size(saved_font_size());
}

/**
 * the program wrote: give it to libvt, and draw in the next frame.
 */
static void pty_ready(int fd, void *data)
{
    (void)data;
    static char buf[16384];
    for (int i = 0; i < READS_MAX; i++)
    {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            vt_write(M.vt, buf, (size_t)n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        // the program ended (EIO): so does the terminal
        idunn_app_watch(M.app, fd, NULL, NULL);
        idunn_app_quit(M.app);
        return;
    }
    idunn_canvas_update(M.canvas);
}

static pid_t start_program(char **argv, int cols, int rows)
{
    struct winsize size = {(unsigned short)rows, (unsigned short)cols, 0, 0};
    int slave;
    if (openpty(&M.master, &slave, NULL, NULL, &size) != 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0)
    {
        close(M.master);
        if (login_tty(slave) != 0)
            _exit(126);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "mimir", 1);
        const char *home = getenv("HOME");
        if (home && chdir(home) != 0)
            chdir("/");
        if (argv[0])
            execvp(argv[0], argv);
        else
            execl(SHELL, "sh", (char *)NULL);
        fprintf(stderr, "mimir: cannot start %s\n", argv[0] ? argv[0] : SHELL);
        _exit(127);
    }
    close(slave);
    if (pid < 0)
        return -1;
    fcntl(M.master, F_SETFL, O_NONBLOCK);
    return pid;
}

int main(int argc, char **argv)
{
    (void)argc;
    M.app = idunn_app_new("mimir");
    if (!M.app)
    {
        printf("mimir: no window server (start the desktop with startde)\n");
        return 1;
    }
    M.scale = theme()->scale;
    M.font_size = saved_font_size();
    if (!load_faces())
    {
        printf("mimir: no font in " FONTS "Regular.ttf\n");
        return 1;
    }
    M.cursor_y = -1;
    M.blink_on = true;
    M.vt = vt_new(80, 24, SCROLLBACK);
    vt_set_reply(M.vt, on_reply, NULL);
    M.child = start_program(argv + 1, 80, 24);
    if (M.child < 0)
    {
        printf("mimir: no pty\n");
        return 1;
    }

    // 80 by 24 cells, in design pixels
    int32_t width = (80 * M.cw + 2 * M.pad + M.scale - 1) / M.scale;
    int32_t height = (24 * M.ch + 2 * M.pad + M.scale - 1) / M.scale;
    M.win = idunn_window_new(M.app, "mimir", width, height);
    if (!M.win)
        return 1;
    static const idunn_canvas_hooks_t hooks = {canvas_draw, canvas_input, canvas_resize,
                                               canvas_frame};
    M.canvas = idunn_canvas(&hooks, NULL);
    idunn_window_set_root(M.win, M.canvas);
    idunn_set_focus(M.canvas);
    idunn_app_watch(M.app, M.master, pty_ready, NULL);
    idunn_app_on_event(M.app, on_event, NULL);

    int status = idunn_app_run(M.app);

    // the window closed first: the program hears it hang up
    kill(M.child, SIGHUP);
    close(M.master);
    waitpid(M.child, NULL, 0);
    return status;
}
