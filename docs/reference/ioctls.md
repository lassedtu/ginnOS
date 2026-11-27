<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# The ioctls

The requests of `ioctl` on devices and sockets, from the libc headers.

## &lt;netinet/in.h&gt;

See [`<netinet/in.h>`](libc/netinet-in.md).

| Request | Number | What it does |
| --- | --- | --- |
| `IPPROTO_TCP` | `6` |  |

## &lt;netinet/tcp.h&gt;

See [`<netinet/tcp.h>`](libc/netinet-tcp.md).

| Request | Number | What it does |
| --- | --- | --- |
| `TCP_NODELAY` | `1` |  |
| `TCP_MAXSEG` | `2` |  |
| `TCP_KEEPIDLE` | `4` |  |
| `TCP_KEEPINTVL` | `5` |  |
| `TCP_KEEPCNT` | `6` |  |

## &lt;sys/fb.h&gt;

See [`<sys/fb.h>`](libc/sys-fb.md).

| Request | Number | What it does |
| --- | --- | --- |
| `FBIOGET_VSCREENINFO` | `0x4600` |  |
| `FBIOPUT_VSCREENINFO` | `0x4601` |  |
| `FBIOGET_FSCREENINFO` | `0x4602` |  |
| `FBIOGET_MODE` | `0x46F0` |  |
| `FBIO_GRAPHICS` | `0x46F1` |  |
| `FBIO_FLUSH` | `0x46F2` |  |
| `FBIO_CURSOR` | `0x46F3` |  |
| `FBIO_CURSOR_MOVE` | `0x46F4` |  |

## &lt;sys/input.h&gt;

See [`<sys/input.h>`](libc/sys-input.md).

| Request | Number | What it does |
| --- | --- | --- |
| `INPUT_IOC_GRAB` | `0x40044901u` | ioctl: while a reader holds the grab, key events go only to /dev/input, not to the text console (the compositor takes the keyboard). int arg: 1 grab, 0 release. closing the reader releases it. |

## &lt;sys/ioctl.h&gt;

See [`<sys/ioctl.h>`](libc/sys-ioctl.md).

| Request | Number | What it does |
| --- | --- | --- |
| `TCGETS` | `0x5401` |  |
| `TCSETS` | `0x5402` |  |
| `TCSETSW` | `0x5403` |  |
| `TCSETSF` | `0x5404` |  |
| `TIOCSCTTY` | `0x540E` |  |
| `TIOCGPGRP` | `0x540F` |  |
| `TIOCSPGRP` | `0x5410` |  |
| `TIOCGWINSZ` | `0x5413` |  |
| `TIOCSWINSZ` | `0x5414` | set the window size (SIGWINCH to the foreground) |
| `TIOCGPTN` | `0x80045430` | pty master: the number N of /dev/pts/N |
| `SIOCGIFFLAGS` | `0x8913` | network interfaces, on any AF\_INET socket, with a struct ifreq (&lt;net/if.h&gt;). the S calls need root. SIOCGIFGATEWAY and SIOCSIFGATEWAY are ginnOS's own: the gateway of the interface, in ifr\_addr. |
| `SIOCSIFFLAGS` | `0x8914` |  |
| `SIOCGIFADDR` | `0x8915` |  |
| `SIOCSIFADDR` | `0x8916` |  |
| `SIOCGIFBRDADDR` | `0x8919` |  |
| `SIOCGIFNETMASK` | `0x891B` |  |
| `SIOCSIFNETMASK` | `0x891C` |  |
| `SIOCGIFMTU` | `0x8921` |  |
| `SIOCGIFHWADDR` | `0x8927` |  |
| `SIOCGIFGATEWAY` | `0x89F0` |  |
| `SIOCSIFGATEWAY` | `0x89F1` |  |
| `TIOCSPTLCK` | `0x40045431` | pty master: lock (1) or unlock (0) the slave |

## &lt;sys/kbd.h&gt;

See [`<sys/kbd.h>`](libc/sys-kbd.md).

| Request | Number | What it does |
| --- | --- | --- |
| `KBD_IOC_LAYOUT_COUNT` | `0x4B80` | out: uint32\_t, how many layouts there are |
| `KBD_IOC_LAYOUT_NAME` | `0x4B81` | in/out: struct kbd\_layout\_name (EINVAL past the end) |
| `KBD_IOC_GET_LAYOUT` | `0x4B82` | out: uint32\_t, the index of the active layout |
| `KBD_IOC_SET_LAYOUT` | `0x4B83` | in: uint32\_t, the index to make active |
| `KBD_IOC_GET_KEYMAP` | `0x4B84` | out: struct kbd\_keymap, the active layout's tables |

## &lt;sys/soundcard.h&gt;

See [`<sys/soundcard.h>`](libc/sys-soundcard.md).

| Request | Number | What it does |
| --- | --- | --- |
| `SNDCTL_DSP_RESET` | `0x00005000u` | stop and drop the queued sound |
| `SNDCTL_DSP_SYNC` | `0x00005001u` | wait until the queued sound has played |
| `SNDCTL_DSP_SPEED` | `0xC0045002u` | int: the sample rate (the device's nearest back) |
| `SNDCTL_DSP_STEREO` | `0xC0045003u` | int: 0 mono, 1 stereo |
| `SNDCTL_DSP_GETBLKSIZE` | `0xC0045004u` | int: the fragment size |
| `SNDCTL_DSP_SETFMT` | `0xC0045005u` | int: AFMT\_ (the format in use back) |
| `SNDCTL_DSP_CHANNELS` | `0xC0045006u` | int: 1 or 2 |
| `SNDCTL_DSP_SETFRAGMENT` | `0xC004500Au` | int: the fragments wanted (the device keeps its own) |
| `SNDCTL_DSP_GETFMTS` | `0x8004500Bu` | int: the formats the device takes |
| `SNDCTL_DSP_GETOSPACE` | `0x8010500Cu` | audio\_buf\_info: free space |
| `SNDCTL_DSP_GETISPACE` | `0x8010500Du` | audio\_buf\_info: recording (none: EINVAL) |
| `SNDCTL_DSP_GETODELAY` | `0x80045017u` | int: bytes queued, not played yet |

## &lt;sys/times.h&gt;

See [`<sys/times.h>`](libc/sys-times.md).

| Request | Number | What it does |
| --- | --- | --- |
| `CLK_TCK` | `100` |  |
