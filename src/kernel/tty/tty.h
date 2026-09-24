#pragma once

#include "common/stdint.h"

/**
 * @file tty.h
 * @brief terminal (TTY) layer.
 *
 * a tty_t is a terminal state machine sitting between byte-oriented callers
 * (kernel printf, the write syscall) and a character-cell backend (the VGA
 * text buffer today, a serial line or framebuffer later). it owns the ANSI
 * escape parser, cursor handling, and line-discipline mode, none of which the
 * backend needs to know about. the backend only has to place a glyph at a
 * cell, scroll, clear, and move the hardware cursor.
 *
 * splitting this out of the VGA driver lets a future system have several
 * terminals (virtual consoles, a serial tty) that share one state machine.
 */

// how many decimal parameters a CSI escape sequence may carry (e.g. ESC[r;cH).
#define TTY_CSI_PARAMS_MAX 4

// line-discipline modes, matching the ttyctl() syscall values.
#define TTY_MODE_COOKED 0 // line-buffered, editable input
#define TTY_MODE_RAW    1 // raw byte/event input, no editing

// ANSI colour indices 0-7 (normal) and 8-15 (bright). the backend maps these
// to its own representation. defaults match a light-grey-on-black console.
#define TTY_COLOR_DEFAULT_FG 7 // light grey
#define TTY_COLOR_DEFAULT_BG 0 // black

/**
 * character-cell backend a tty draws onto. put_at/scroll/clear/set_cursor/
 * get_cursor/rows/cols are required; set_colors and draw_cursor are optional
 * (NULL is fine) so a backend that has no notion of colour or needs no
 * software caret can omit them.
 * coordinates are zero-based (row, col); the backend defines its own bounds.
 */
typedef struct
{
    void (*put_at)(char c, uint8_t row, uint8_t col); // place a glyph at a cell
    void (*scroll)(void);                             // scroll up one row
    void (*clear)(void);                              // clear screen, cursor home
    void (*set_cursor)(uint8_t row, uint8_t col);     // move the cursor
    void (*get_cursor)(uint8_t *row, uint8_t *col);   // read the cursor
    // set the fg/bg used by subsequent put_at calls, as ANSI colour indices
    // (0-15). optional; NULL means the backend is monochrome.
    void (*set_colors)(uint8_t fg, uint8_t bg);
    // draw (or erase) a caret at a cell. optional; NULL means the backend has
    // its own cursor (e.g. VGA hardware cursor).
    void (*draw_cursor)(uint8_t row, uint8_t col, bool visible);
    // push buffered drawing to the display. optional; NULL means the backend
    // draws directly (e.g. VGA text buffer, or an unbuffered framebuffer).
    void (*flush)(void);
    uint8_t rows;                                     // backend height in cells
    uint8_t cols;                                     // backend width in cells
} tty_backend_t;

// ANSI escape parser states.
typedef enum
{
    TTY_STATE_NORMAL,   // ordinary character output
    TTY_STATE_ESC,      // saw ESC (0x1B)
    TTY_STATE_CSI,      // saw ESC [
    TTY_STATE_CSI_PRIV, // saw ESC [ ? (DEC private mode)
} tty_state_t;

/**
 * a terminal instance.
 */
typedef struct
{
    const tty_backend_t *backend; // where glyphs and cursor moves go
    tty_state_t state;            // ANSI parser state
    int csi_params[TTY_CSI_PARAMS_MAX]; // collected CSI numeric parameters
    int csi_param_count;          // how many parameters have been separated
    int csi_current_param;        // parameter currently being accumulated
    uint8_t mode;                 // TTY_MODE_COOKED or TTY_MODE_RAW
    uint8_t fg;                   // current foreground ANSI colour index (0-15)
    uint8_t bg;                   // current background ANSI colour index (0-15)
    bool bold;                    // bold/bright attribute (brightens fg)
} tty_t;

/**
 * initialize a tty on a backend. starts in cooked mode, normal parser state.
 * @param tty the terminal to initialize.
 * @param backend the character-cell backend (must outlive the tty).
 */
void tty_init(tty_t *tty, const tty_backend_t *backend);

/**
 * feed one output byte through the terminal state machine.
 * printable characters are drawn; control characters and ANSI escape
 * sequences are interpreted (cursor movement, erase, etc.).
 * @param tty the terminal.
 * @param c the byte to process.
 */
void tty_putchar(tty_t *tty, char c);

/**
 * feed a null-terminated string through tty_putchar().
 * @param tty the terminal.
 * @param str the string to write.
 */
void tty_write(tty_t *tty, const char *str);

/**
 * set the line-discipline mode (TTY_MODE_COOKED or TTY_MODE_RAW).
 * @param tty the terminal.
 * @param mode the new mode.
 * @return the previous mode.
 */
uint8_t tty_set_mode(tty_t *tty, uint8_t mode);
