<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# gfx

libgfx: drawing into pixel buffers.

## &lt;gfx.h&gt;

Source: [`src/lib/gfx/include/gfx.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/gfx/include/gfx.h)

libgfx: drawing into pixel buffers in memory.

every buffer has one pixel format: 32 bits per pixel, premultiplied
ARGB (0xAARRGGBB in a uint32\_t, each colour already multiplied by the
alpha). premultiplied pixels make blending one multiply per channel,
and scaling and blending give no dark edges. only gfx\_present changes
the format, when it copies a buffer to the framebuffer.

coordinates are pixels, x to the right and y down, with (0, 0) at the
top left. a rectangle includes its left and top edges and excludes its
right and bottom edges. every drawing call is clipped to the buffer's
clip rectangle, so drawing outside the buffer is safe and does nothing.

the drawing is integer only, so the host and ginnOS make the same
pixels (the host tests compare golden images).

### gfx\_color\_t

```c
typedef uint32_t gfx_color_t;
```

a premultiplied ARGB colour, 0xAARRGGBB. make one with gfx\_rgb or
gfx\_rgba.

### gfx\_format\_t

```c
typedef struct
{
    uint8_t bytes_per_pixel; // 2, 3 or 4
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
} gfx_format_t;
```

the pixel format of a framebuffer, for gfx\_present. the sizes and
shifts are those of the framebuffer's FBIOGET\_VSCREENINFO.

### gfx\_rgba

```c
gfx_color_t gfx_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
```

a colour with alpha, from straight (not premultiplied) channels.

### gfx\_fade

```c
gfx_color_t gfx_fade(gfx_color_t c, uint8_t opacity);
```

a colour with its alpha (and so its premultiplied channels) scaled by
opacity / 255.

### gfx\_over

```c
gfx_color_t gfx_over(gfx_color_t dst, gfx_color_t src);
```

a source pixel over a destination pixel (Porter-Duff source over).

### gfx\_mix

```c
gfx_color_t gfx_mix(gfx_color_t a, gfx_color_t b, uint8_t amount);
```

a mix of two colours, as CSS color-mix in sRGB: amount of b (0 to 255)
and the rest of a.

### gfx\_rect\_intersect

```c
gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b);
```

the part two rectangles share (empty if none).

### gfx\_rect\_union

```c
gfx_rect_t gfx_rect_union(gfx_rect_t a, gfx_rect_t b);
```

the smallest rectangle that holds both (an empty one counts as none).

### gfx\_buffer\_new

```c
gfx_buffer_t *gfx_buffer_new(int32_t width, int32_t height);
```

a new buffer of transparent pixels, from malloc.

Returns: the buffer, or NULL (size 0 or out of memory).

### gfx\_buffer\_free

```c
void gfx_buffer_free(gfx_buffer_t *buf);
```

free a buffer from gfx\_buffer\_new (and its pixels, if it owns them).

### gfx\_buffer\_init

```c
void gfx_buffer_init(gfx_buffer_t *buf, uint32_t *pixels, int32_t width, int32_t height,
                     int32_t stride);
```

make a buffer over memory the caller owns (a shared memory mapping, for
example). the clip rectangle is the whole buffer.

- `stride`: pixels from one row to the next.

### gfx\_buffer\_sub

```c
gfx_buffer_t gfx_buffer_sub(const gfx_buffer_t *parent, gfx_rect_t r);
```

a buffer for a part of another buffer: the same pixels, its own clip.
the rectangle is clipped to the parent. it is not freed on its own.

### gfx\_set\_clip

```c
void gfx_set_clip(gfx_buffer_t *buf, gfx_rect_t clip);
```

set the clip rectangle (it is clipped to the buffer).

### gfx\_reset\_clip

```c
void gfx_reset_clip(gfx_buffer_t *buf);
```

make the whole buffer the clip rectangle.

### gfx\_clear

```c
void gfx_clear(gfx_buffer_t *buf, gfx_color_t color);
```

set every pixel in the clip rectangle to a colour (no blending).

### gfx\_fill\_rect

```c
void gfx_fill_rect(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t color);
```

fill a rectangle, blended over what is there.

### gfx\_stroke\_rect

```c
void gfx_stroke_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t thickness, gfx_color_t color);
```

the edges of a rectangle, thickness pixels wide, on the inside.

### gfx\_fill\_round\_rect

```c
void gfx_fill_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, gfx_color_t color);
```

fill a rectangle with round corners, with anti-aliased corners (as CSS
border-radius).

- `radius`: the corner radius (made smaller if the rectangle is small).

### gfx\_fill\_round\_rect\_gradient

```c
void gfx_fill_round_rect_gradient(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius,
                                  gfx_color_t top, gfx_color_t bottom);
```

fill a round rectangle with a vertical gradient from top to bottom,
sampled at the pixel centres (as a CSS linear-gradient).

### gfx\_border\_round\_rect

```c
void gfx_border_round_rect(gfx_buffer_t *buf, gfx_rect_t r, int32_t radius, int32_t width,
                           gfx_color_t color);
```

the border of a round rectangle: width pixels wide, inside r (as a CSS
border with box-sizing: border-box). the inside is not touched.

### gfx\_shadow\_t

```c
typedef struct
{
    int32_t x;      // the offset
    int32_t y;
    int32_t blur;   // the CSS blur radius: a Gaussian blur with sigma = blur / 2
    int32_t spread; // grows the shape (shrinks it for an inset shadow)
    gfx_color_t color;
    bool inset;     // inside the box instead of outside it
} gfx_shadow_t;
```

a CSS box-shadow: "x y blur spread color", and "inset".

### gfx\_box\_shadow

```c
void gfx_box_shadow(gfx_buffer_t *buf, gfx_rect_t box, int32_t radius, const gfx_shadow_t *shadow);
```

draw a box-shadow of a round box. an outer shadow is drawn only outside
the box and an inset one only inside it, as in CSS: draw outer shadows
before the box, inset ones after its background.

- `box`: the border box (an inset shadow: the box inside the border).

### gfx\_line

```c
void gfx_line(gfx_buffer_t *buf, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
              gfx_color_t color);
```

a line one pixel wide from (x0, y0) to (x1, y1), both ends included,
blended over what is there.

### gfx\_copy

```c
void gfx_copy(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
              gfx_rect_t src_rect);
```

copy a part of src to dst at (x, y), replacing the pixels (no blending).
the buffers may be the same one, and the parts may overlap.

### gfx\_blend

```c
void gfx_blend(gfx_buffer_t *dst, int32_t x, int32_t y, const gfx_buffer_t *src,
               gfx_rect_t src_rect, uint8_t opacity);
```

blend a part of src over dst at (x, y), with an opacity (255: as it is).

### gfx\_blend\_scaled

```c
void gfx_blend_scaled(gfx_buffer_t *dst, gfx_rect_t dst_rect, const gfx_buffer_t *src,
                      gfx_rect_t src_rect, uint8_t opacity);
```

draw a part of src scaled to fill dst\_rect, blended over dst, with
bilinear filtering.

### gfx\_draw\_mask

```c
void gfx_draw_mask(gfx_buffer_t *dst, int32_t x, int32_t y, const uint8_t *mask, int32_t width,
                   int32_t height, int32_t stride, gfx_color_t color);
```

a coverage mask (one byte per pixel, 0 to 255) in a colour, blended
over dst with its top left at (x, y). text and line icons are drawn
this way.

- `stride`: bytes from one mask row to the next.

### gfx\_fill\_gradient

```c
void gfx_fill_gradient(gfx_buffer_t *buf, gfx_rect_t r, gfx_color_t top, gfx_color_t bottom);
```

a vertical gradient from top to bottom colour over a rectangle
(replacing the pixels). for backgrounds.

### gfx\_blur

```c
void gfx_blur(gfx_buffer_t *buf, double sigma);
```

blur the whole buffer like a Gaussian with this sigma (in pixels); the
edge pixels are used again past the edges.

### gfx\_present

```c
void gfx_present(void *fb, uint32_t fb_pitch, int32_t fb_width, int32_t fb_height,
                 const gfx_format_t *format, const gfx_buffer_t *src, gfx_rect_t src_rect);
```

copy a part of a buffer to a framebuffer, in its pixel format. the
pixels are taken as over black (alpha is dropped).

- `fb`: the framebuffer's first pixel.
- `fb_pitch`: bytes from one framebuffer row to the next.
- `fb_width`: the framebuffer's size, to clip to.
- `src_rect`: the part of src; it goes to the same place on the framebuffer.
