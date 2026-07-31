# Ginnung OS

## Prerequisites

To build this project on macOS, install:

- Xcode Command Line Tools: `xcode-select --install`
- Homebrew
- Build dependencies for the OSDev cross-compiler guide:
  - `gmp`
  - `mpfr`
  - `libmpc`
  - `texinfo`
- Tools used by this repository:
  - `nasm`
  - `qemu` or `qemu-system-i386`

The cross-compiler is expected at `~/opt/cross/bin`, with `i686-elf-gcc` and `i686-elf-ld` available on your `PATH`.

## Build

Run the build script from the repository root:

```sh
./build.sh
```

This creates `bin/os.bin`.

## Boot

After building, start the OS with:

```sh
qemu-system-i386 -m 64M -no-reboot -drive format=raw,file=bin/os.bin
```

## Notes

- If `./build.sh` cannot find `i686-elf-gcc`, make sure your cross-compiler is installed under `~/opt/cross` and that `~/opt/cross/bin` is on `PATH`.
- If QEMU is installed under a different binary name on your system, use that executable instead.
