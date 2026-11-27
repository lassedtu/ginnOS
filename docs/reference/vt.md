<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# vt

libvt: a terminal emulator without a screen.

## &lt;vt.h&gt;

Source: [`src/lib/vt/include/vt.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/vt/include/vt.h)

libvt: a terminal emulator without a screen.

the bytes a program writes to its terminal go in (vt\_write); a grid of
cells comes out, with the scrollback, the cursor and the modes the
program set. libvt also makes the bytes for keys, the mouse and paste,
as the program asked for them (vt\_key, vt\_mouse, vt\_paste\_begin).

```c
vt_t *vt = vt_new(80, 24, 10000);
vt_write(vt, buf, n);                // what the pty gave
for (int y = 0; y < vt->rows; y++)
    if (vt_line_dirty(vt, y))
        draw_line(vt_line(vt, y));
vt_clean(vt);
```

the escape sequences are those of xterm that full-screen programs use:
VT100 and VT220 cursor and erase, scroll regions, insert and delete,
SGR with 16, 256 and 24-bit colours, the alternate screen, bracketed
paste, mouse reports (X10, button and any-event, SGR), cursor shapes,
the DEC line drawing characters and the window title (OSC 0 and 2).
TERM is xterm-256color.

not yet: wide characters (CJK, emoji) take one cell, combining marks
take a cell of their own, and the lines are not wrapped again on a
resize.

### VT\_COLOR\_DEFAULT, VT\_COLOR\_INDEX, VT\_COLOR\_RGB, VT\_COLOR\_KIND

```c
#define VT_COLOR_DEFAULT 0u
#define VT_COLOR_INDEX   0x01000000u // | the palette index (0 to 255)
#define VT_COLOR_RGB     0x02000000u // | 0xRRGGBB
#define VT_COLOR_KIND(c) ((c) & 0xFF000000u)
```

a colour of a cell: the default (the theme's text or background), one
of the 256 palette colours, or a 24-bit colour.

### vt\_line\_t

```c
typedef struct
{
    uint16_t cols;  // the width it was made with
    bool wrapped;   // the text goes on in the next line (for copy)
    bool dirty;     // changed since vt_clean (screen lines only)
    vt_cell_t cells[];
} vt_line_t;
```

a line of cells: on the screen, or in the scrollback.

### vt\_reply\_t

```c
typedef void (*vt_reply_t)(const char *data, size_t length, void *user);
```

where the terminal sends its answers (the cursor position, the device
attributes): to the program, through the pty.

### vt\_new, vt\_free

```c
vt_t *vt_new(int cols, int rows, int scrollback);
void vt_free(vt_t *vt);
```

a terminal of a size, with room for so many lines of scrollback.

### vt\_set\_reply

```c
void vt_set_reply(vt_t *vt, vt_reply_t reply, void *user);
```

where the answers go (without it, they are dropped).

### vt\_write

```c
void vt_write(vt_t *vt, const void *data, size_t length);
```

the bytes the program wrote. UTF-8 and escape sequences can be split
between calls.

### vt\_resize

```c
void vt_resize(vt_t *vt, int cols, int rows);
```

a new size. the lines keep their text (cut, or filled with empty
cells); when the screen gets lower, the lines above the cursor go into
the scrollback so the cursor stays on the screen.

### vt\_line

```c
const vt_line_t *vt_line(const vt_t *vt, int y);
```

a line of the screen (0 to rows - 1), or of the scrollback (-1 is the
newest line, down to -vt\_scrollback\_lines). NULL past the ends. a line
of the scrollback can be narrower or wider than the screen.

### vt\_scrollback\_lines

```c
int vt_scrollback_lines(const vt_t *vt);
```

the lines in the scrollback now

### vt\_line\_dirty

```c
bool vt_line_dirty(const vt_t *vt, int y);
```

true if the screen line y changed since vt\_clean

### vt\_clean

```c
void vt_clean(vt_t *vt);
```

forget what changed: the app drew it

### vt\_text

```c
size_t vt_text(const vt_t *vt, int x0, int y0, int x1, int y1, char *out, size_t size);
```

the text from (x0, y0) to (x1, y1), both included, as UTF-8; y as for
vt\_line. lines end with "\n" unless they wrapped; the empty cells at
the end of a line are left out.

Returns: the bytes it needed (it writes at most size - 1 and a 0).

### vt\_palette

```c
uint32_t vt_palette(int index);
```

the RGB colour of palette entry 0 to 255: the 16 colours of xterm, the
6x6x6 cube and the grey ramp. the app can use its own 16 colours.

### vt\_key

```c
int vt_key(const vt_t *vt, vt_key_t key, int mods, char *out);
```

the bytes a key sends, as xterm sends them (the arrows follow
DECCKM; with modifiers, ESC \[ 1 ; m A).

- `out`: room for 16 bytes.

Returns: the length (0: the key sends nothing).

### vt\_text\_key

```c
int vt_text_key(const char *text, int mods, char *out);
```

the bytes typed text sends: Ctrl with a letter or one of @\[\]^\_ and
space gives the control character, Alt puts ESC in front.

- `text`: UTF-8, as the keyboard layout made it.
- `out`: room for 16 bytes.

Returns: the length.

### vt\_mouse

```c
int vt_mouse(const vt_t *vt, vt_mouse_t kind, int button, int x, int y, int mods, char *out);
```

a mouse report for the program, if its mode asks for this one.

- `button`: 0 left, 1 middle, 2 right, -1 none (motion).
- `x,`: y the cell, from 0.
- `out`: room for 32 bytes.

Returns: the length (0: nothing to send; the app can select instead).

### vt\_paste\_begin, vt\_paste\_end

```c
const char *vt_paste_begin(const vt_t *vt);
const char *vt_paste_end(const vt_t *vt);
```

what goes before and after pasted text: the bracketed paste marks when
the program asked for them, else "".
