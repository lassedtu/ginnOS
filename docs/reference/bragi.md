<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# bragi

libbragi: TrueType text.

## &lt;bragi.h&gt;

Source: [`src/lib/bragi/include/bragi.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/bragi/include/bragi.h)

bragi: TrueType text for libgfx buffers.

a font is a TrueType file in memory. a face is a font at one size, with
its own glyph cache. text is UTF-8; bad bytes show as U+FFFD.

glyphs are anti-aliased (8-bit coverage, from stb\_truetype) and placed
at quarter pixels, so the spacing of text stays even. the coverage is
blended in sRGB, as browsers do, so text has the same weight as in the
idunn reference.

a face can have a fallback face: a character the font does not have
comes from the fallback (and from its fallback, and so on). with none,
the font's own "missing glyph" box is drawn.

### bragi\_font\_open

```c
bragi_font_t *bragi_font_open(const char *path);
```

load a TrueType file.

Returns: the font, or NULL (no file, or not a TrueType font).

### bragi\_font\_from\_memory

```c
bragi_font_t *bragi_font_from_memory(const uint8_t *data, size_t size, bool take);
```

a font from memory. with take, the font frees data when it is closed;
else data must stay until then.

### bragi\_font\_close

```c
void bragi_font_close(bragi_font_t *font);
```

close a font. close its faces first.

### bragi\_face\_new

```c
bragi_face_t *bragi_face_new(bragi_font_t *font, float pixel_size);
```

the font at a size.

- `pixel_size`: the size of the em in pixels (what "16 px text" means); 4 to 512.

Returns: the face, or NULL.

### bragi\_face\_set\_fallback

```c
void bragi_face_set_fallback(bragi_face_t *face, bragi_face_t *fallback);
```

where characters the font does not have come from (NULL: none).

### bragi\_face\_set\_tracking

```c
void bragi_face_set_tracking(bragi_face_t *face, float em);
```

letter spacing, as CSS letter-spacing: added after every character, so
measuring, wrapping and drawing all include it.

- `em`: in ems (-0.006 for Inter body text in idunn).

### bragi\_line\_baseline

```c
int32_t bragi_line_baseline(const bragi_face_t *face, int32_t line_height);
```

where the baseline is in a line of text line\_height pixels high, from
its top: as CSS places text in a line box (the font's ascent and
descent, with half of the rest above them).

### bragi\_ascent, bragi\_descent, bragi\_line\_height

```c
int32_t bragi_ascent(const bragi_face_t *face);
int32_t bragi_descent(const bragi_face_t *face);
int32_t bragi_line_height(const bragi_face_t *face);
```

the vertical metrics in whole pixels: from the baseline up to the top
of the highest letters (ascent), down to the bottom of the lowest
(descent, a positive number), and the distance from one baseline to
the next (line height).

### bragi\_has\_char

```c
bool bragi_has_char(const bragi_face_t *face, uint32_t codepoint);
```

true if the font (not a fallback) has a glyph for the character.

### bragi\_utf8\_next

```c
uint32_t bragi_utf8_next(const char **text, const char *end);
```

decode one character of UTF-8.

- `text`: the bytes; \*text moves past the character.
- `end`: the end of the bytes.

Returns: the code point; a bad sequence gives U+FFFD and moves one byte.

### bragi\_measure

```c
int32_t bragi_measure(bragi_face_t *face, const char *text, int32_t length);
```

the width of a line of text in pixels (rounded up). a newline does not
end it: use the layout calls for more than one line.

- `length`: bytes of text, or -1 for up to the terminating zero.

### bragi\_draw

```c
int32_t bragi_draw(gfx_buffer_t *buf, bragi_face_t *face, int32_t x, int32_t baseline,
                   const char *text, int32_t length, gfx_color_t color);
```

draw a line of text.

- `x`: the left end of the text.
- `baseline`: the y of the baseline.

Returns: the advance width in pixels (where the next text would start).

### bragi\_caret\_x

```c
int32_t bragi_caret_x(bragi_face_t *face, const char *text, int32_t length, int32_t offset);
```

where a caret before the byte at offset goes, in pixels from the start
of the line (rounded to the nearest pixel).

- `offset`: a byte offset at the start of a character; it is cut to the text.

### bragi\_offset\_at

```c
int32_t bragi_offset_at(bragi_face_t *face, const char *text, int32_t length, int32_t x);
```

the caret place nearest to x (pixels from the start of the line), for
a click in a text field.

Returns: a byte offset at the start of a character, or the length.

### bragi\_wrap

```c
int32_t bragi_wrap(bragi_face_t *face, const char *text, int32_t width, const char **next);
```

how many bytes of a line fit in a width, broken at a space if one is in
the part that fits, else inside a word (at least one character).

- `next`: receives where the following line starts (after the spaces and the newline the line ends with).

Returns: the bytes of this line (without trailing spaces; 0 for an empty line). the text is at its end when \*\*next is '\0'.

### bragi\_draw\_box

```c
int32_t bragi_draw_box(gfx_buffer_t *buf, bragi_face_t *face, gfx_rect_t box, const char *text,
                       bragi_align_t align, gfx_color_t color);
```

draw a paragraph in a box: wrapped to the box's width, aligned, and as
many lines as fit in its height. if not all the text fits, the last
line ends with "..." (U+2026 when the font has it).

Returns: the height of the lines drawn.

### bragi\_measure\_box

```c
int32_t bragi_measure_box(bragi_face_t *face, const char *text, int32_t width);
```

the height a paragraph needs at a width (all of it, nothing cut off).

### bragi\_cell\_size

```c
void bragi_cell_size(bragi_face_t *face, int32_t *width, int32_t *height);
```

the cell of a monospace face, in whole pixels: the advance of "M" and
the line height.

### bragi\_draw\_cell

```c
void bragi_draw_cell(gfx_buffer_t *buf, bragi_face_t *face, int32_t x, int32_t y,
                     uint32_t codepoint, gfx_color_t color);
```

draw one character in a cell, its top left at (x, y). the background
is not drawn. the single and heavy box drawing lines and the block
elements are drawn by code to fill the cell, so they join from cell to
cell; the double lines and the diagonals come from the font.
