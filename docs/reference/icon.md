<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# icon

libicon: the icon packs.

## &lt;icon.h&gt;

Source: [`src/lib/icon/include/icon.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/icon/include/icon.h)

icons from the icon packs in /usr/share/icons.

two sets: "lucide" has line icons as coverage masks, drawn in a colour
given at draw time (window controls, toolbars); "tela" has full colour
icons (apps, places, file types, devices). tools/icons/build.py makes
the packs; the format is described there.

an icon is loaded at a size and a scale (1, or 2 for a 2x screen). when
the pack has no image of that many pixels, the next larger one is
scaled down (or the largest one up).

### icon\_set\_open

```c
icon_set_t *icon_set_open(const char *path);
```

open a pack file.

Returns: the set, or NULL (no file, or not an icon pack).

### icon\_set\_named

```c
icon_set_t *icon_set_named(const char *name);
```

open a set by its name: ICON\_DIR "&lt;name&gt;.pack".

### icon\_set\_is\_mask

```c
bool icon_set_is_mask(const icon_set_t *set);
```

true if the set is of masks (lucide), false if of colour images (tela).

### icon\_set\_has

```c
bool icon_set_has(const icon_set_t *set, const char *name);
```

true if the set has an icon of exactly this name.

### icon\_load

```c
icon_t *icon_load(icon_set_t *set, const char *name, int32_t size, int32_t scale);
```

load an icon.

a name the set does not have falls back as in the freedesktop icon
naming spec: the last part after a dash is removed until a name is
found (text-x-csrc, text-x, text), then "unknown".

- `size`: the icon size in points (16, 20, 24, 32, 48, 64, or any).
- `scale`: 1 to 4 (2 for a 2x screen); the icon is size \* scale pixels, at most 1024.

Returns: the icon (free it with icon\_free), or NULL.

### icon\_draw\_mask

```c
void icon_draw_mask(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y,
                    gfx_color_t color);
```

draw an icon in a colour: a mask's coverage, or a colour image's alpha.

### icon\_draw

```c
void icon_draw(gfx_buffer_t *buf, const icon_t *icon, int32_t x, int32_t y, uint8_t opacity);
```

draw an icon as it is, with an opacity (255: opaque). a mask is drawn
in black.
