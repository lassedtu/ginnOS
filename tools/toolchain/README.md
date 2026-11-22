# The ginnOS toolchain

A GCC and binutils that know ginnOS as a target (`x86_64-ginnos`,
`i686-ginnos`). Programs and ports build with them as on any Unix:

```sh
x86_64-ginnos-gcc -O2 hello.c -o hello
./configure --host=x86_64-ginnos && make
```

The compiler defines `__ginnos__` and `__unix__`, links statically, and
finds the libc headers and libraries in the sysroot.

## Build it

```sh
tools/toolchain/build.sh            # x86_64
tools/toolchain/build.sh i686
export PATH="$PWD/tools/toolchain/prefix/bin:$PATH"
```

It downloads binutils and GCC, applies `patches/`, and installs into
`tools/toolchain/prefix`. On macOS it needs `brew install gmp mpfr libmpc
isl`. It takes a while; build it once, and again only when the patches or
the versions change.

## The sysroot

`make sysroot` (and `make ARCH=x86_64 sysroot`) puts the libc headers,
`crt0.o`, `crti.o`, `crtn.o` and `libc.a` into `build/<arch>/sysroot`,
where the compiler looks for `/usr/include` and `/usr/lib`. Run it again
after a change to the libc.

## What the patches do

- binutils: `config.sub` knows `ginnos`; bfd, gas and ld treat it like a
  plain ELF target (the `elf_x86_64` and `elf_i386` linker modes).
- GCC: `config.sub` knows `ginnos`; `gcc/config/ginnos.h` sets the start
  files, `-lc`, static linking, 4 KiB pages and the `__ginnos__` macro;
  libgcc builds `crtbegin.o` and `crtend.o`; fixincludes is skipped.

To change a patch: edit the sources in `src/`, then make the patch again
with `diff -ruN` against a fresh copy of the archive.

## How long it takes

Measured on an 8-core Apple Silicon Mac (2026-11-22), x86_64:

| Step | Time |
| --- | --- |
| download (about 140 MB) and unpack | 1 to 2 minutes |
| binutils | about 5 minutes |
| GCC, the C and C++ compilers | about 12 minutes |
| libgcc and the install | about 1 minute |

i686 the same way with `build.sh i686`: about 20 minutes in all, from a
fresh unpack of the sources.

The GCC log shows thousands of warnings from building GCC itself; they
are normal. The logs are in `build/gcc-<arch>/`.

## Try it

```sh
x86_64-ginnos-gcc -O2 tools/toolchain/test/hello.c -o hello
```

Copy `hello` to `tools/ext2/rootfs/bin/` (not in git), `make ARCH=x86_64`,
boot, and run `hello one two` (`i686-ginnos-gcc` and plain `make` for the
32-bit system). It prints
`hello from ginnOS: 3 argument(s), hello, 9223372036854775807`.

## What the toolchain asked of the libc

Building libgcc against our libc found the first gaps. Each port will
find more; note them here.

- `int64_t` and the other types now come from the compiler
  (`__INT64_TYPE__`): a 64-bit Unix compiler uses `long`, and our
  `long long` did not agree. The `PRI*64` and `PRI*PTR` formats follow.
- `<unistd.h>` declares `intptr_t`, as POSIX says.
- `execv` and `execvp` (with a PATH search) were missing.

## Not yet

- `crt0` does not call `_init` or the `.init_array` constructors: C++
  static objects do not run yet (step U5 of `plans/userspace-plan.md`).
- No libstdc++: `x86_64-ginnos-g++` compiles, but C++ programs that use
  the standard library do not link yet (U5).
- Our own programs still build with the bare `x86_64-elf-` compiler
  through `src/user/program.mk`; moving them over comes later.
