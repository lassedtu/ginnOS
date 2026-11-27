<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;fb.h&gt;

Source: [`src/libc/include/fb.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/fb.h)

userspace access to the linear framebuffer.

a program calls fbinfo() to learn the framebuffer geometry and pixel format,
then fbmap() to map it into its address space and draw pixels directly.
these are convenience wrappers over the /dev/fb0 fbdev (open + ioctl + mmap,
see &lt;sys/fb.h&gt;); a program can use that interface directly instead. this is
still a single-client path (no compositor yet): whoever maps it owns the
whole screen until it exits.

## fb\_info\_t

```c
typedef struct
{
    uint32_t width;   // width in pixels.
    uint32_t height;  // height in pixels.
    uint32_t pitch;   // bytes per scanline (may exceed width * bpp/8).
    uint32_t bpp;     // bits per pixel (24 or 32).
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
    uint8_t _pad[2];
} fb_info_t;
```

framebuffer geometry and pixel format, filled by fbinfo().
mirrors the kernel's view; the channel size/shift pairs describe how r/g/b
sit inside a pixel so a program can pack colours for any layout.

## fbinfo

```c
int fbinfo(fb_info_t *info);
```

query the framebuffer geometry and pixel format.

- `info`: output struct.

Returns: 0 on success, -1 if there is no framebuffer.

## fbmap

```c
void *fbmap(void);
```

map the framebuffer into this process's address space.

Returns: a writable pointer to the framebuffer, or NULL on failure. the returned buffer is `pitch * height` bytes; write packed pixels.

## pollkey

```c
int pollkey(void);
```

non-blocking keyboard poll for interactive graphics programs.

Returns: the next pending character key, or 0 if none is waiting.
