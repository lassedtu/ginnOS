# ginnOS

ginnOS is a Unix-like operating system for x86 PCs, written from scratch
in C and assembly. It runs as a 32-bit (i686) or a 64-bit (x86_64)
system, boots with Limine on BIOS and UEFI, and has its own desktop,
apps, network stack and C library.

The code is at <https://github.com/lassedtu/ginnOS>.

## The parts of this manual

| Part | For | Pages |
| --- | --- | --- |
| [Use ginnOS](use/README.md) | people who use ginnOS | [Get started](use/getting-started.md), [The desktop](use/desktop.md), [The apps](use/apps.md), [The shell](use/shell.md), [The network](use/networking.md), [The settings files](use/settings-files.md) |
| [Write programs](develop/README.md) | people who write or port programs for ginnOS | [Set up the build tools](develop/setup.md), [Build ginnOS](develop/building.md), [The ginnOS toolchain](develop/toolchain.md), [libc](develop/libc.md), [The libraries](develop/libraries.md), [Port a program](develop/porting.md) |
| [How ginnOS works](internals/README.md) | people who work on ginnOS itself | [The architecture](internals/architecture.md), [The boot](internals/boot.md), [Memory](internals/memory.md), [Processes](internals/processes.md), [Signals](internals/signals.md), [The file systems](internals/filesystems.md), [The network stack](internals/network.md), [The drivers](internals/drivers.md), [Random numbers](internals/random.md), [The desktop stack](internals/desktop.md), [Testing](internals/testing.md) |
| [Reference](reference/README.md) | everyone who calls an interface | [The system calls](reference/syscalls.md), [The ioctls](reference/ioctls.md), [libc](reference/libc/README.md), and a page for each library |

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
