# How ginnOS works

This part is for people who work on ginnOS itself: the boot, the kernel,
the drivers, the libraries and the desktop, and how to build and test
them.

- [The architecture](architecture.md): from the power button to the
  desktop, and how the parts talk.
- [Build ginnOS](../develop/building.md): the build, the tests and the
  debugger.
- The code style: [`STYLE.md`](https://github.com/lassedtu/ginnOS/blob/main/STYLE.md).

## The source tree

| Folder | What it contains |
| --- | --- |
| `src/arch/x86/` | the code for i686 and x86_64: the boot entry, the GDT, the IDT, the paging, the context switch, the system call entry. The other folders in `src/arch/` are empty |
| `src/bootloader/` | the old two-stage BIOS bootloader (`BOOTLOADER=custom`) |
| `src/common/` | code that the kernel and the old bootloader share: strings, memory, printf, the boot information |
| `src/kernel/` | the kernel: memory, processes, the scheduler, signals, the VFS, the system calls, the network stack, randomness, the TTY, sound |
| `src/drivers/` | the device drivers: disks, keyboard, mouse, PCI, USB, sound, video, virtio, the timers, the serial port |
| `src/fs/ext2/` | the ext2 filesystem |
| `src/test/` | the kernel tests for `make test` |
| `src/libc/` | the C library, with its headers in `src/libc/include/` and its tests in `src/libc/test/` |
| `src/lib/` | the libraries for programs: gfx, bragi, idunn, heimdall, conf, apps, mime, icon, image, vt |
| `src/user/` | the programs: init, the shell, the desktop and its apps, the network tools, the test programs |
| `ports/` | the build scripts for programs from other projects |
| `linker/` | the linker scripts |
| `mk/` | the parts of the build |
| `tools/` | the tools for the build: the disk image, Limine, the toolchain, the fonts, the icons, the wallpaper, gdb |
