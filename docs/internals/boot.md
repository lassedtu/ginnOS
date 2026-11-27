# The boot

ginnOS boots with Limine, on BIOS and on UEFI. Limine loads the kernel
and the root file system image, then starts the kernel with the
Multiboot 2 protocol (i686) or the Limine protocol (x86_64). For the
steps after that, see [The architecture](architecture.md).

The code: [`tools/limine/`](https://github.com/lassedtu/ginnOS/tree/main/tools/limine),
[`src/arch/x86/boot/`](https://github.com/lassedtu/ginnOS/tree/main/src/arch/x86/boot) (i686),
[`src/arch/x86/boot64/`](https://github.com/lassedtu/ginnOS/tree/main/src/arch/x86/boot64) (x86_64),
[`src/common/boot/boot_info.h`](https://github.com/lassedtu/ginnOS/blob/main/src/common/boot/boot_info.h)

## The disk image

[`tools/limine/make_disk.py`](https://github.com/lassedtu/ginnOS/blob/main/tools/limine/make_disk.py)
makes `build/ginnos.img` (for x86_64, `build/x86_64/ginnos.img`). It has
two partitions, each on a 1 MiB boundary:

| Partition | Type | What it has |
| --- | --- | --- |
| 1 | FAT, the EFI system partition | Limine, `limine.conf`, `/boot/kernel.elf` and `/boot/rootfs.ext2` |
| 2 | ext2, label `GINNOS_EXT2` | the root file system |

The partition table is MBR. With `make GPT=1` it is GPT, with a BIOS
boot partition for the BIOS part of Limine. The BIOS part of Limine is
in the MBR and in the gap before partition 1. The script fills the FAT
partition with mtools, so it needs no root access.

## limine.conf

The configuration is
[`limine-i686.conf`](https://github.com/lassedtu/ginnOS/blob/main/tools/limine/limine-i686.conf)
or
[`limine-x86_64.conf`](https://github.com/lassedtu/ginnOS/blob/main/tools/limine/limine-x86_64.conf).
[`boot_menu.py`](https://github.com/lassedtu/ginnOS/blob/main/tools/limine/boot_menu.py)
adds a "Screen sizes" folder to the boot menu, with an entry for each size, for a
computer whose firmware does not give the size of the monitor.

| Build option | Default | What it does |
| --- | --- | --- |
| `BOOT_TIMEOUT` | `3` | the seconds that the boot menu waits. A key stops the count. |
| `BOOT_SIZES` | `3840x2160` to `1024x768` | the screen sizes in the boot menu |
| `BOOT_RESOLUTION` | empty | the screen size of the first entry. Empty: Limine picks the size of the monitor, or the mode of the firmware. |
| `BOOTLOADER` | `limine` | `custom` is the old BIOS-only bootloader in `src/bootloader/` (i686 only) |

## The two kernels

| | i686 | x86_64 |
| --- | --- | --- |
| protocol | Multiboot 2 | Limine (base revision 6) |
| entry | [`boot/multiboot2.c`](https://github.com/lassedtu/ginnOS/blob/main/src/arch/x86/boot/multiboot2.c) | [`asm64/entry.asm`](https://github.com/lassedtu/ginnOS/blob/main/src/arch/x86/asm64/entry.asm), [`boot64/limine_boot.c`](https://github.com/lassedtu/ginnOS/blob/main/src/arch/x86/boot64/limine_boot.c) |
| CPU mode at the start | 32-bit protected mode, no paging | 64-bit long mode, Limine's page tables |
| where the kernel runs | `0xC0000000` and up | `0xFFFFFFFF80000000` and up |

The i686 kernel reads the Multiboot 2 tags. The x86_64 kernel puts
request structures in its image. Limine fills a response for each, before
it starts the kernel.

## boot_info_t

Each entry copies what the bootloader gives into one `boot_info_t`, and
calls `kernel_main` with it. The rest of the kernel reads only this
structure:

- the memory map;
- the framebuffer: address, size, pitch and pixel format;
- the ACPI RSDP;
- the command line;
- the boot module (the root file system image).

A field that a boot protocol cannot give is zero, for example
`framebuffer_addr == 0` for no framebuffer.

## The root file system at boot

Limine always loads `rootfs.ext2` as a module. The kernel uses it only
if it does not find the `GINNOS_EXT2` partition on a disk, for example
on a USB stick, where ginnOS has no driver for the disk. Then the root
file system is in RAM, and changes go at reboot.
