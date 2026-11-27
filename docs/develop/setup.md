# Set up the build tools

This page tells which tools you need to build ginnOS, and how to install
them on macOS, Linux, Windows and Nix.

The code: [`mk/dev.mk`](https://github.com/lassedtu/ginnOS/blob/main/mk/dev.mk)
(`check-tools`), [`tools/toolchain/`](https://github.com/lassedtu/ginnOS/tree/main/tools/toolchain),
[`shell.nix`](https://github.com/lassedtu/ginnOS/blob/main/shell.nix)

## The tools

The build uses two kinds of compiler:

- An `i686-elf` cross compiler builds the kernel, libc and the ginnOS
  programs. The 64-bit build (`ARCH=x86_64`) uses an `x86_64-elf` cross
  compiler.
- The ginnOS toolchain (`i686-ginnos`, `x86_64-ginnos`) builds the ports,
  the programs from other projects. See [the toolchain](toolchain.md).

| Tool | What uses it |
| --- | --- |
| `i686-elf-gcc`, `ld`, `objcopy`, `nm` | the kernel, libc and the programs |
| `x86_64-elf-gcc` and its binutils | the 64-bit build |
| `nasm` | the assembly files |
| `qemu-system-i386`, `qemu-system-x86_64` | `make run` and `make test` |
| `python3` | the image, font and icon tools |
| `mtools` (`mformat`) | the FAT boot partition for Limine |
| `e2fsprogs` (`mke2fs`) | the ext2 root filesystem |
| `git` | to get Limine and the ports |
| `curl` | to get the fonts, the icons and the port sources |
| `rsvg-convert` (librsvg) or `resvg` | the icon packs (`ICONS=0` builds without them) |
| gmp, mpfr, libmpc, isl | to build the ginnOS toolchain |
| `cmake` | the SDL and curl ports |
| `yacc` | the sbase port |
| Pillow (Python) | the wallpaper (optional) and the console font tool |
| `gdb` | `make gdb` and `make debug` (optional) |
| OVMF | `make run-uefi` (optional) |

`make` stops with `missing tool: <name>` if it does not find a tool
that it needs.

## macOS

1. Install the host tools with Homebrew:

   ```sh
   brew install gmp mpfr libmpc isl texinfo nasm qemu mtools e2fsprogs cmake librsvg
   brew install x86_64-elf-gcc
   brew install gdb
   ```

   The second line is for the 64-bit build. The third line is for
   `make gdb` and is optional. Homebrew's QEMU has the OVMF firmware.

2. Build the `i686-elf` cross compiler. See
   [Build the i686-elf cross compiler](#build-the-i686-elf-cross-compiler).
   Use `JOBS=$(sysctl -n hw.ncpu)` and add the `PATH` line to `~/.zshrc`.

3. Build the ginnOS toolchain. See [the toolchain](toolchain.md).

## Ubuntu and Debian

1. Install the host tools:

   ```sh
   sudo apt update
   sudo apt install build-essential curl nasm qemu-system-x86 \
     libgmp-dev libmpfr-dev libmpc-dev libisl-dev texinfo bison flex \
     python3 python3-pil e2fsprogs mtools git ovmf cmake librsvg2-bin
   sudo apt install gdb
   ```

   The second command is for `make gdb` and is optional.

2. Build the `i686-elf` cross compiler. See
   [Build the i686-elf cross compiler](#build-the-i686-elf-cross-compiler).
   Use `JOBS=$(nproc)` and add the `PATH` line to `~/.bashrc` (or
   `~/.zshrc` for zsh).

3. For the 64-bit build, build an `x86_64-elf` cross compiler in the
   same way, with `--target=x86_64-elf`.

4. Build the ginnOS toolchain. See [the toolchain](toolchain.md).

## Windows

Build ginnOS in WSL2 with Ubuntu.

1. Open PowerShell as Administrator and install Ubuntu:

   ```powershell
   wsl --install -d Ubuntu
   ```

   Restart Windows if it asks you to. Ubuntu then asks you to make a
   Linux user account.

2. Open Ubuntu from the Start menu, or type `wsl` in PowerShell.

3. Clone the project into the Linux filesystem, for example into
   `~/projects/ginnOS`. Do not build in `/mnt/c/...`: file access across
   the Windows and Linux border is slow, and `make` can see incorrect
   file times there.

   ```sh
   mkdir -p ~/projects
   cd ~/projects
   git clone https://github.com/lassedtu/ginnOS
   cd ginnOS
   ```

4. Do the steps in [Ubuntu and Debian](#ubuntu-and-debian) in the WSL
   terminal. QEMU opens in its own window on Windows 11.

## Nix

`shell.nix` has the `i686-elf` and `x86_64-elf` cross compilers,
binutils, NASM, QEMU, Python 3, gdb, mtools, e2fsprogs, git and OVMF.
Start the shell in the project folder:

```sh
nix-shell
```

The shell does not have gmp, mpfr, libmpc and isl for the ginnOS
toolchain, `cmake` for the SDL and curl ports, or librsvg for the icons.
Without them, build an image with no ports and no icons:

```sh
make PORTS= ICONS=0
```

## Build the i686-elf cross compiler

Do these steps on macOS and on Linux. Set `JOBS` to the number of CPUs
first.

1. Make the folders:

   ```sh
   mkdir -p "$HOME/src/cross" "$HOME/opt/cross"
   cd "$HOME/src/cross"
   ```

2. Build and install binutils:

   ```sh
   curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
   tar -xf binutils-2.42.tar.xz
   mkdir -p build-binutils
   cd build-binutils
   ../binutils-2.42/configure --target=i686-elf --prefix="$HOME/opt/cross" \
     --with-sysroot --disable-nls --disable-werror
   make -j"$JOBS"
   make install
   cd ..
   ```

3. Build and install GCC (only the C compiler):

   ```sh
   curl -LO https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
   tar -xf gcc-14.2.0.tar.xz
   mkdir -p build-gcc
   cd build-gcc
   ../gcc-14.2.0/configure --target=i686-elf --prefix="$HOME/opt/cross" \
     --disable-nls --enable-languages=c --without-headers
   make -j"$JOBS" all-gcc all-target-libgcc
   make install-gcc install-target-libgcc
   cd ..
   ```

4. Add the compiler to `PATH` in your shell's start file:

   ```sh
   export PATH="$HOME/opt/cross/bin:$PATH"
   ```

5. Examine the tools:

   ```sh
   i686-elf-gcc --version
   nasm -v
   qemu-system-i386 --version
   ```

If an i686 build stops with `unknown pseudo-op: .base64`, the cross GCC
is newer than its binutils: GCC 16 needs binutils 2.43 or later. The
build then looks for newer binutils from Homebrew
(`brew install i686-elf-binutils`). You can also update the binutils of
your cross compiler.

## Next

Build and run ginnOS: see [Build ginnOS](building.md).
