#pragma once

/**
 * @file vt.h
 * @brief libvt: a terminal emulator without a screen.
 *
 * the bytes a program writes to its terminal go in (vt_write); a grid of
 * cells comes out, with the scrollback, the cursor and the modes the
 * program set. libvt also makes the bytes for keys, the mouse and paste,
 * as the program asked for them (vt_key, vt_mouse, vt_paste_begin).
 *
 *   vt_t *vt = vt_new(80, 24, 10000);
 *   vt_write(vt, buf, n);                // what the pty gave
 *   for (int y = 0; y < vt->rows; y++)
 *       if (vt_line_dirty(vt, y))
 *           draw_line(vt_line(vt, y));
 *   vt_clean(vt);
 *
 * the escape sequences are those of xterm that full-screen programs use:
 * VT100 and VT220 cursor and erase, scroll regions, insert and delete,
 * SGR with 16, 256 and 24-bit colours, the alternate screen, bracketed
 * paste, mouse reports (X10, button and any-event, SGR), cursor shapes,
 * the DEC line drawing characters and the window title (OSC 0 and 2).
 * TERM is xterm-256color.
 *
 * not yet: wide characters (CJK, emoji) take one cell, combining marks
 * take a cell of their own, and the lines are not wrapped again on a
 * resize.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * a colour of a cell: the default (the theme's text or background), one
 * of the 256 palette colours, or a 24-bit colour.
 */
#define VT_COLOR_DEFAULT 0u
#define VT_COLOR_INDEX   0x01000000u // | the palette index (0 to 255)
#define VT_COLOR_RGB     0x02000000u // | 0xRRGGBB
#define VT_COLOR_KIND(c) ((c) & 0xFF000000u)

// cell attributes (vt_cell_t.attr)
#define VT_BOLD      0x0001u
#define VT_DIM       0x0002u
#define VT_ITALIC    0x0004u
#define VT_UNDERLINE 0x0008u
#define VT_BLINK     0x0010u
#define VT_INVERSE   0x0020u
#define VT_HIDDEN    0x0040u
#define VT_STRIKE    0x0080u

typedef struct
{
    uint32_t ch; // the code point; 0 is an empty cell (drawn as a space)
    uint32_t fg; // VT_COLOR_
    uint32_t bg;
    uint16_t attr; // VT_BOLD and the others
} vt_cell_t;

/**
 * a line of cells: on the screen, or in the scrollback.
 */
typedef struct
{
    uint16_t cols;  // the width it was made with
    bool wrapped;   // the text goes on in the next line (for copy)
    bool dirty;     // changed since vt_clean (screen lines only)
    vt_cell_t cells[];
} vt_line_t;

typedef enum
{
    VT_CURSOR_BLOCK,
    VT_CURSOR_UNDERLINE,
    VT_CURSOR_BAR,
} vt_cursor_shape_t;

// the mouse reports a program asked for (vt_t.mouse)
typedef enum
{
    VT_MOUSE_OFF,
    VT_MOUSE_CLICKS, // ?1000: presses and releases
    VT_MOUSE_DRAG,   // ?1002: and motion with a button down
    VT_MOUSE_MOTION, // ?1003: and all motion
} vt_mouse_mode_t;

/**
 * where the terminal sends its answers (the cursor position, the device
 * attributes): to the program, through the pty.
 */
typedef void (*vt_reply_t)(const char *data, size_t length, void *user);

typedef struct vt vt_t;

struct vt
{
    // read these; change them only through the functions
    int cols;
    int rows;
    int cursor_x; // 0 to cols - 1
    int cursor_y; // 0 to rows - 1
    bool cursor_visible;
    bool cursor_blink;
    vt_cursor_shape_t cursor_shape;
    bool alt_screen;      // the alternate screen is shown (no scrollback)
    bool app_cursor_keys; // DECCKM: arrows send ESC O A
    bool app_keypad;
    bool bracketed_paste;
    vt_mouse_mode_t mouse;
    bool mouse_sgr;       // ?1006: SGR mouse reports
    bool focus_reports;   // ?1004
    bool reverse_video;   // ?5: the whole screen in inverse
    char title[256];      // from OSC 0 or 2
    bool title_changed;   // set when the title changes; the app clears it
    bool bell;            // a BEL came; the app clears it
    int scrolled;         // lines the whole screen moved up since vt_clean

    struct vt_private *p; // the parser, the screens, the scrollback
};

/**
 * a terminal of a size, with room for so many lines of scrollback.
 */
vt_t *vt_new(int cols, int rows, int scrollback);
void vt_free(vt_t *vt);

/**
 * where the answers go (without it, they are dropped).
 */
void vt_set_reply(vt_t *vt, vt_reply_t reply, void *user);

/**
 * the bytes the program wrote. UTF-8 and escape sequences can be split
 * between calls.
 */
void vt_write(vt_t *vt, const void *data, size_t length);

/**
 * a new size. the lines keep their text (cut, or filled with empty
 * cells); when the screen gets lower, the lines above the cursor go into
 * the scrollback so the cursor stays on the screen.
 */
void vt_resize(vt_t *vt, int cols, int rows);

/**
 * a line of the screen (0 to rows - 1), or of the scrollback (-1 is the
 * newest line, down to -vt_scrollback_lines). NULL past the ends. a line
 * of the scrollback can be narrower or wider than the screen.
 */
const vt_line_t *vt_line(const vt_t *vt, int y);

/** the lines in the scrollback now */
int vt_scrollback_lines(const vt_t *vt);

/** true if the screen line y changed since vt_clean */
bool vt_line_dirty(const vt_t *vt, int y);

/** forget what changed: the app drew it */
void vt_clean(vt_t *vt);

/**
 * the text from (x0, y0) to (x1, y1), both included, as UTF-8; y as for
 * vt_line. lines end with "\n" unless they wrapped; the empty cells at
 * the end of a line are left out.
 * @return the bytes it needed (it writes at most size - 1 and a 0).
 */
size_t vt_text(const vt_t *vt, int x0, int y0, int x1, int y1, char *out, size_t size);

/**
 * the RGB colour of palette entry 0 to 255: the 16 colours of xterm, the
 * 6x6x6 cube and the grey ramp. the app can use its own 16 colours.
 */
uint32_t vt_palette(int index);

// keys that are not text
typedef enum
{
    VT_KEY_UP = 1,
    VT_KEY_DOWN,
    VT_KEY_RIGHT,
    VT_KEY_LEFT,
    VT_KEY_HOME,
    VT_KEY_END,
    VT_KEY_INSERT,
    VT_KEY_DELETE,
    VT_KEY_PAGE_UP,
    VT_KEY_PAGE_DOWN,
    VT_KEY_ENTER,
    VT_KEY_TAB,
    VT_KEY_BACKSPACE,
    VT_KEY_ESCAPE,
    VT_KEY_F1, // to VT_KEY_F12 in order
    VT_KEY_F12 = VT_KEY_F1 + 11,
} vt_key_t;

// modifier keys for vt_key, vt_text_key and vt_mouse
#define VT_MOD_SHIFT 0x01
#define VT_MOD_ALT   0x02
#define VT_MOD_CTRL  0x04

/**
 * the bytes a key sends, as xterm sends them (the arrows follow
 * DECCKM; with modifiers, ESC [ 1 ; m A).
 * @param out room for 16 bytes.
 * @return the length (0: the key sends nothing).
 */
int vt_key(const vt_t *vt, vt_key_t key, int mods, char *out);

/**
 * the bytes typed text sends: Ctrl with a letter or one of @[\]^_ and
 * space gives the control character, Alt puts ESC in front.
 * @param text UTF-8, as the keyboard layout made it.
 * @param out room for 16 bytes.
 * @return the length.
 */
int vt_text_key(const char *text, int mods, char *out);

typedef enum
{
    VT_MOUSE_PRESS,
    VT_MOUSE_RELEASE,
    VT_MOUSE_MOVE, // with a button down, or (button -1) with none
    VT_MOUSE_WHEEL_UP,
    VT_MOUSE_WHEEL_DOWN,
} vt_mouse_t;

/**
 * a mouse report for the program, if its mode asks for this one.
 * @param button 0 left, 1 middle, 2 right, -1 none (motion).
 * @param x, y the cell, from 0.
 * @param out room for 32 bytes.
 * @return the length (0: nothing to send; the app can select instead).
 */
int vt_mouse(const vt_t *vt, vt_mouse_t kind, int button, int x, int y, int mods, char *out);

/**
 * what goes before and after pasted text: the bracketed paste marks when
 * the program asked for them, else "".
 */
const char *vt_paste_begin(const vt_t *vt);
const char *vt_paste_end(const vt_t *vt);
