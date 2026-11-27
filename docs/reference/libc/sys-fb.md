<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/fb.h&gt;

Source: [`src/libc/include/sys/fb.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/fb.h)

framebuffer device interface (userspace).

the fbdev model: open "/dev/fb0", ioctl for geometry, mmap for pixels.
the screeninfo structs and FBIO\* codes are ABI and must match the kernel in
src/common/fb/fb\_ioctl.h exactly.

## fb\_mode\_t

```c
typedef struct
{
    uint32_t index;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t current;   // 1 for the mode in use
    uint32_t preferred; // 1 for the size the screen wants (the QEMU window)
} fb_mode_t;
```

FBIOGET\_MODE: one video mode. the caller sets index (0, 1, ...); the
kernel fills in the rest, or fails with EINVAL past the last mode. with
a display driver the list is the sizes it can set, the preferred one
first; without one it is the firmware's list from the bootloader (or
only the current mode).

## FBIO\_GRAPHICS

```c
#define FBIO_GRAPHICS 0x46F1
```

FBIO\_GRAPHICS: int arg 1 takes the screen for this process, 0 gives it
back. while a process has the screen, the text console does not draw on
it (its text is kept, and comes back when the screen is given back). the
screen comes back by itself when the process ends, and on a kernel
panic. EBUSY if another live process has it.

## fb\_rect\_t

```c
typedef struct
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} fb_rect_t;
```

FBIO\_FLUSH: arg is an fb\_rect\_t of pixels that changed. a display that
does not show the memory by itself (virtio-gpu) copies them to the
screen; for the others it does nothing. a mapped framebuffer is only
sure to be seen after this.

## FB\_CURSOR\_SIZE

```c
#define FB_CURSOR_SIZE 64
```

FBIO\_CURSOR: arg is an fb\_cursor\_t. a display that has a pointer of its
own (virtio-gpu) shows pixels over the screen, with their top left
corner at x, y; shown 0 hides it. only the owner of the screen
(FBIO\_GRAPHICS) can use it, and the pointer goes when the owner gives
the screen back or ends. ENOTTY when the display has no pointer: draw
it in the picture then.

FBIO\_CURSOR\_MOVE: arg is an fb\_point\_t, the new top left corner.

## fb\_event\_t

```c
typedef struct
{
    uint32_t type; // FB_EVENT_*
    uint32_t width;
    uint32_t height;
} fb_event_t;
```

FBIOPUT\_VSCREENINFO sets the mode to xres x yres at 32 bits per pixel
when a display driver can (EINVAL if it cannot). the framebuffer can
move: map it again after a change.

/dev/display: read gives fb\_event\_t records; poll says when one is
there. a record comes when the mode changed and when the size the
screen wants changed (the QEMU window was resized).

## fb\_open

```c
int fb_open(void);
```

open the framebuffer device (/dev/fb0).

Returns: an open fd on success, or -1 on error (errno set).
