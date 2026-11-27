<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/kbd.h&gt;

Source: [`src/libc/include/sys/kbd.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/kbd.h)

the ioctls of /dev/kbd: list, read and set the keyboard layout.

the values and the struct are ABI and must match
src/drivers/keyboard/keyboard\_ioctl.h.

## kbd\_layout\_name

```c
struct kbd_layout_name
{
    uint32_t index;
    char name[KBD_LAYOUT_NAME_MAX];
};
```

KBD\_IOC\_LAYOUT\_NAME: set index; the kernel fills in name.

## kbd\_keymap

```c
struct kbd_keymap
{
    char normal[KBD_KEYMAP_KEYS];
    char shift[KBD_KEYMAP_KEYS];
    char altgr[KBD_KEYMAP_KEYS];
    char altgr_shift[KBD_KEYMAP_KEYS];
};
```

KBD\_IOC\_GET\_KEYMAP: the characters of the active layout, by key code
(the Linux key code of /dev/input, below 128). 0: the key gives no
character. the characters are ASCII. altgr is for either Alt key (the
Option keys on a Mac keyboard), as on the text console.
