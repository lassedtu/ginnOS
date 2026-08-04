# GinnOS
GinnOS (Ginn short for Ginnungagap, and OS short for Operating System) is a personal hobby operating system, which i'm developing alongside a course on operating systems i'm taking at DTU.

I started this project because i wanted to understand how an operating system is really coded instead of just theory. I plan to keep building and improving it as i learn more.

The name GinnOS is taken from Ginnungagap, the primordial void in Norse mythology from which the world was created.

> "That was the age when nothing was; / There was no sand, nor sea, nor cool waves, / No earth nor sky nor grass there, / Only Ginnungagap."
> — Völuspá, Poetic Edda

## macOS Toolchain Setup (Manual)

### Install host dependencies

Only install what this two-stage build needs:

```bash
brew install gmp mpfr libmpc texinfo nasm qemu
```

### Build i686 cross compiler manually

Create build directories:

```bash
mkdir -p "$HOME/src/cross"
mkdir -p "$HOME/opt/cross"
cd "$HOME/src/cross"
```

Build and install binutils:

```bash
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
tar -xf binutils-2.42.tar.xz
mkdir -p build-binutils
cd build-binutils

../binutils-2.42/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --with-sysroot \
  --disable-nls \
  --disable-werror

make -j"$(sysctl -n hw.ncpu)"
make install
cd ..
```

Build and install GCC (C only):

```bash
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
tar -xf gcc-14.2.0.tar.xz
mkdir -p build-gcc
cd build-gcc

../gcc-14.2.0/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --disable-nls \
  --enable-languages=c \
  --without-headers

make -j"$(sysctl -n hw.ncpu)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..
```

### Add cross toolchain to PATH

```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### Verify required tools

```bash
i686-elf-gcc --version
i686-elf-ld --version
i686-elf-objcopy --version
nasm -v
qemu-system-i386 --version
```

## Build and Run

From project root:

```bash
make
make run
```

## ext2 Side-Disk Helper

The ext2 helper now builds an image from a real folder tree.

Default root folder:

- [tools/ext2/rootfs](tools/ext2/rootfs)

Generate an ext2 image using the default folder:

```bash
make ext2-image
make run-ext2
```

Generate an ext2 image from any folder on your machine:

```bash
make ext2-image EXT2_SOURCE_DIR=/absolute/path/to/your/folder
```

Optionally force a custom image size (MiB):

```bash
make ext2-image EXT2_SOURCE_DIR=/absolute/path/to/your/folder EXT2_SIZE_MB=64
```

If omitted, the helper auto-sizes the image from folder contents.

If you want the ext2 helper available on macOS, install:

```bash
brew install e2fsprogs
```

The helper tool is located at:

- [tools/ext2/make_image.py](tools/ext2/make_image.py)

## Credits and Learning Resources

- Nanobyte: https://www.youtube.com/@nanobyte-dev
  Operating system tutorials that have been a big help.

- OSDev Wiki: https://wiki.osdev.org/
  The main reference used for low level concepts and implementation.