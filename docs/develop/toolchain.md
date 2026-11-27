# The ginnOS toolchain

The ginnOS toolchain is a GCC and binutils that know ginnOS as a target:
`i686-ginnos` and `x86_64-ginnos`. The ports and your own programs build
with it as on other Unix systems.

The code: [`tools/toolchain/`](https://github.com/lassedtu/ginnOS/tree/main/tools/toolchain)
(the build script and the patches),
[`mk/libc.mk`](https://github.com/lassedtu/ginnOS/blob/main/mk/libc.mk) (the sysroot)

## What the compiler does

- It defines `__ginnos__` and `__unix__`.
- It links statically. ginnOS has no shared libraries.
- It finds the libc headers in `/usr/include` and the libraries in
  `/usr/lib` of the sysroot.
- It uses `crt0.o`, `crti.o`, `crtn.o` and `-lc` from the sysroot, and
  `crtbegin.o` and `crtend.o` from libgcc.

The patches teach `config.sub`, bfd, gas and ld the `ginnos` name, and
add `gcc/config/ginnos.h` with the start files, the libraries, static
linking, 4 KiB pages and the `__ginnos__` macro.

## Build the toolchain

1. Install gmp, mpfr, libmpc and isl. See [Set up the build tools](setup.md).

2. Build the toolchain for each architecture that you use. Run the
   commands in the project folder:

   ```sh
   tools/toolchain/build.sh x86_64
   tools/toolchain/build.sh i686
   ```

   The script gets binutils and GCC (about 140 MB), applies the patches,
   and installs into `tools/toolchain/prefix`. One architecture takes
   about 20 minutes on an 8-core Apple Silicon Mac. The logs are in
   `tools/toolchain/build/`. The GCC log has thousands of warnings from
   GCC itself; they are normal.

3. Add the toolchain to `PATH`:

   ```sh
   export PATH="$PWD/tools/toolchain/prefix/bin:$PATH"
   ```

Build the toolchain again only when the patches or the versions change.

## The sysroot

The sysroot is the folder where the compiler finds the headers and the
libraries: `build/sysroot` for i686 and `build/x86_64/sysroot` for
x86_64. Make it with:

```sh
make sysroot
make ARCH=x86_64 sysroot
```

It contains the libc headers, `crt0.o`, `crti.o`, `crtn.o`, `libc.a`,
an empty `libm.a` and `libpthread.a` (the math and the threads are in
`libc.a`), and `libgfx.a` and `libheimdall.a`. The library ports (SDL 3,
mbedTLS, curl) add their headers and archives when `make` builds them.

Make the sysroot again after you change libc.

## Compile a program

```sh
x86_64-ginnos-gcc -O2 hello.c -o hello
```

The result is a static ELF64 program. A program with a `configure`
script builds with:

```sh
./configure --host=x86_64-ginnos
make
```

For a CMake project, use the toolchain file and give it the compiler
prefix:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=tools/toolchain/cmake/ginnos.cmake \
      -DGINNOS_CROSS="$PWD/tools/toolchain/prefix/bin/x86_64-ginnos-" ..
```

## Run the program on ginnOS

1. Copy the program into `tools/ext2/rootfs/bin/`. Git ignores that
   folder.
2. Build the image for the same architecture, for example
   `make ARCH=x86_64`.
3. Start ginnOS (`make run ARCH=x86_64`) and run the program in the
   terminal.

`tools/toolchain/test/hello.c` is a small test program. It prints its
arguments and a 64-bit number.

## Limits

- `crt0` does not call `_init` or the `.init_array` constructors, so
  the constructors of C++ static objects do not run.
- There is no libstdc++. `x86_64-ginnos-g++` compiles, but a C++ program
  that uses the standard library does not link.
- The ginnOS programs in `src/user/` build with the `i686-elf` and
  `x86_64-elf` compilers, not with this toolchain.
