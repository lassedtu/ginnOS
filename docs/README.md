# ginnOS

ginnOS is a Unix-like operating system for x86 PCs, written from scratch
in C and assembly. It runs as a 32-bit (i686) or a 64-bit (x86_64)
system, boots with Limine on BIOS and UEFI, and has its own desktop,
apps, network stack and C library.

The code is at <https://github.com/lassedtu/ginnOS>.

## The parts of this manual

| Part | For | Start here |
| --- | --- | --- |
| [Use ginnOS](use/README.md) | people who use ginnOS | [Get started](use/getting-started.md) |
| [Write programs](develop/README.md) | people who write or port programs for ginnOS | [Set up the build tools](develop/setup.md) |
| [How ginnOS works](internals/README.md) | people who work on ginnOS itself | [The source tree](internals/README.md#the-source-tree) |

## Quick start

On a machine with the build tools (see
[Set up the build tools](develop/setup.md)):

```sh
git clone https://github.com/lassedtu/ginnOS
cd ginnOS
make
make run
```

Log in as `user` with the password `user`.

## About these pages

The pages are in the `docs/` folder of the ginnOS repository, next to
the code. [How to write these docs](WRITING.md) has the rules for the
pages.
