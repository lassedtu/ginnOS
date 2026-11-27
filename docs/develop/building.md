# Build ginnOS

`make` builds a disk image with the kernel, the programs, the ports and
the root filesystem. This page tells how to build the image, which
options the build has, and how to test and debug the kernel.

The code: [`Makefile`](https://github.com/lassedtu/ginnOS/blob/main/Makefile)
and the parts in [`mk/`](https://github.com/lassedtu/ginnOS/tree/main/mk).
The top of the `Makefile` tells what each part does.

## Build the image

Set up the tools first: see [Set up the build tools](setup.md). Then run
in the project folder:

```sh
make
```

The result is `build/ginnos.img`. For the 64-bit system:

```sh
make ARCH=x86_64
```

The 64-bit build goes into `build/x86_64/`, so the two builds can be on
the disk at the same time. Every target takes `ARCH`.

The first build gets a pinned Limine release into `build/limine`, the
fonts into `build/fonts`, the icon packs into `build/icons` and the port
sources into `build/sources`. `make clean` keeps them. `make distclean`
removes all of `build/`.

The disk image has two partitions:

1. A FAT boot partition with Limine, `limine.conf`, the kernel and a copy
   of the root image.
2. The ext2 root filesystem, with the label `GINNOS_EXT2`.

## The build options

Give an option on the `make` command line, for example
`make run RESOLUTION=1280x800`.

| Option | Default | What it does |
| --- | --- | --- |
| `ARCH` | `i686` | `x86_64` builds the 64-bit system |
| `PORTS` | `sbase lua sdl3 mbedtls ca-certificates curl` | the ports to build; `PORTS=` builds none |
| `RESOLUTION` | `1440x900` | the size of the QEMU window and of the wallpaper |
| `QEMU_VGA` | `virtio` | `std` gives the plain VGA card, fixed at `RESOLUTION` |
| `BOOT_RESOLUTION` | empty | the screen size that the first boot entry asks for; empty lets Limine select it |
| `BOOT_TIMEOUT` | `3` | the seconds that the boot menu waits |
| `AUDIO` | `hda` | `ac97` gives an AC'97 sound card, `0` none |
| `QEMU_AUDIODEV` | `coreaudio` on macOS, `pa` on Linux | the QEMU audio backend |
| `NET` | `1` | `0` gives no network card |
| `GPT` | empty | `1` makes a GPT disk in place of an MBR disk |
| `BOOTLOADER` | `limine` | `custom` uses the old two-stage BIOS bootloader (i686 only) |
| `DOOM` | `0` | `1` builds Doom and adds the Freedoom game data |
| `FREEDOOM` | the value of `DOOM` | `0` leaves out the Freedoom data (28 MiB) |
| `FONTS` | `1` | `0` leaves out the TrueType fonts |
| `ICONS` | `1` | `0` leaves out the icon packs |
| `WALLPAPER` | `.hidden/orbitals/6_3_1.png` | the wallpaper picture; empty leaves it out, and heimdalld draws a gradient |
| `EXT2_SOURCE_DIR` | `tools/ext2/rootfs` | the folder that the root filesystem starts from |
| `EXT2_SIZE_MB` | empty | the size of the root filesystem in MiB; empty makes it as large as the files need |

An image with `PORTS=` has no `ls`, `cat` or the other Unix tools (they
come from sbase), no `curl`, and `fetch` has no HTTPS.

## The root filesystem

`make` copies `EXT2_SOURCE_DIR`, the ginnOS programs and the ports into
one folder, then
[`tools/ext2/make_image.py`](https://github.com/lassedtu/ginnOS/blob/main/tools/ext2/make_image.py)
makes the ext2 image from it with `mke2fs`. All files belong to root
with mode 0755 or 0644. The exceptions are in
[`tools/ext2/permissions`](https://github.com/lassedtu/ginnOS/blob/main/tools/ext2/permissions).

To add your own files, put them in `tools/ext2/rootfs/`. Git ignores
`tools/ext2/rootfs/bin/` and `tools/ext2/rootfs/boot/`.

## Run the image

```sh
make run
```

QEMU starts with a virtio-vga card, an Intel HDA sound card, and a
virtio-net card on the QEMU user network. For more, see
[Get started](../use/getting-started.md).

## Test

There are two test suites:

```sh
make test
make host-test
```

- `make test` builds a test kernel, boots it in QEMU without a window,
  and runs the kernel tests. It prints `PASS` or `FAIL` for each test and
  stops with the result, for example `KTEST: 92 passed, 0 failed`.
- `make host-test` builds the tests of the libraries and programs for
  the build machine and runs them there. Some tests compare a drawn
  picture with a golden image. `make host-test GOLDEN=update` writes the
  golden images again.

Before a commit, run `make test`, build i686 and x86_64 with no
warnings, and start the image in QEMU.

## Look for 64-bit problems

```sh
make check64
```

`make check64` compiles the kernel, the drivers, the filesystems and the
tests for x86_64 with clang and its strict warnings. It does not build
or run an image. It needs `clang` on the build machine. It stops on a
cast between a pointer and an integer, on a 64-bit to 32-bit narrowing,
and on a printf format that does not agree with its argument.

## Debug the kernel

```sh
make gdb
make debug
make gdb GDB_PORT=1240
```

`make gdb` starts QEMU halted with a gdb stub. gdb connects with
`build/kernel.elf` and
[`tools/gdb/ginnos.gdb`](https://github.com/lassedtu/ginnOS/blob/main/tools/gdb/ginnos.gdb),
which sets breakpoints on `kernel_panic` and `kernel_panic_at`. Type `c`
to boot. QEMU stops when gdb stops. `make debug` does the same with a
kernel built with `-Og -DDEBUG`, which is easier to step through. Use
`GDB_PORT` if port 1234 is in use.

gdb must know i386. The gdb of Homebrew and of most Linux distributions
does. Set `GDB=` to use another gdb, for example `GDB=i686-elf-gdb`.

A kernel panic or a CPU exception prints a backtrace with function
names on the screen and on the serial port. You can often find the
problem without a debugger.

## Other tools

| Command | What it does |
| --- | --- |
| `make lint` | runs clang-tidy with `.clang-tidy` |
| `make format` | formats the code with clang-format and `.clang-format` |
| `make compile_commands` | writes `compile_commands.json` for an editor |
| `make sysroot` | makes the sysroot for the toolchain (see [the toolchain](toolchain.md)) |
| `make docs` | makes the reference pages in `docs/reference/` from the comments in the source |
| `make docs-check` | fails if `docs/reference/` is not what the source makes |

## The console font

The framebuffer console draws text with a bitmap font. The kernel only
copies the bitmaps; it does not draw TrueType at run time.
[`tools/font/make_font.py`](https://github.com/lassedtu/ginnOS/blob/main/tools/font/make_font.py)
makes the bitmaps from a TrueType font and writes the C header
`src/drivers/video/fb/font.h`. The header is in git, so the build does
not need the font.

The font is JetBrains Mono Regular in a 10x20 cell. To make the header
again with a different font, size or cell:

```sh
python3 tools/font/make_font.py \
  --font /path/to/Font-Regular.ttf --size 16 --cell 10x20 \
  src/drivers/video/fb/font.h
```

The tool needs Pillow (`pip install Pillow`). JetBrains Mono has the SIL
Open Font License 1.1; the tool copies the notice into the header.
