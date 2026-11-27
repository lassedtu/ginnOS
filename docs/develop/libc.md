# libc

libc is the C library of ginnOS. All programs link it statically: there
are no shared libraries and no dynamic loader. Most of libc is ginnOS's
own code. The regular expressions, `fnmatch`, and the conversion of
floating-point numbers to and from text are from musl.

The code: [`src/libc/`](https://github.com/lassedtu/ginnOS/tree/main/src/libc).
The headers, with a doc comment for each function, are in
[`src/libc/include/`](https://github.com/lassedtu/ginnOS/tree/main/src/libc/include).
The [reference](https://lassedtu.github.io/ginnOS-docs/reference/) has a page for each
header.

## How to use it

Compile with the ginnOS toolchain. It finds the headers and `libc.a` in
the sysroot. See [The ginnOS toolchain](toolchain.md).

`libc.a` contains the math and the threads, so `-lm` and `-lpthread`
are not necessary. The sysroot has empty `libm.a` and `libpthread.a` so
that build scripts that add `-lm` or `-lpthread` still link.

## What it has

| Area | Headers |
| --- | --- |
| Strings, memory, characters | `string.h`, `strings.h`, `ctype.h`, `wchar.h`, `wctype.h` (UTF-8) |
| Streams, `printf`, `scanf` | `stdio.h` |
| Memory, numbers, sort, environment | `stdlib.h`, `search.h` (`tsearch`, `lsearch`) |
| Math | `math.h` (with the `float` functions), `inttypes.h` |
| Files and folders | `unistd.h`, `fcntl.h`, `dirent.h`, `sys/stat.h`, `libgen.h`, `utime.h`, `sys/file.h` (`flock`) |
| Processes and signals | `unistd.h` (`fork`, the `exec` family), `sys/wait.h`, `signal.h` (`sigaction`), `setjmp.h`, `sys/resource.h` |
| Threads | `pthread.h` (threads, mutexes, condition variables, read-write locks, barriers, keys, `pthread_once`), `semaphore.h`, `_Thread_local` |
| Time | `time.h`, `sys/time.h`, `sys/times.h` |
| Terminals | `termios.h`, `pty.h`, `sys/ioctl.h` |
| Memory maps | `sys/mman.h` (`mmap`, `mprotect`, and `shm_create` for shared memory) |
| Waiting on many files | `poll.h`, `sys/select.h` |
| Network | `sys/socket.h`, `sys/un.h`, `netinet/in.h`, `netinet/tcp.h`, `arpa/inet.h`, `netdb.h` (`getaddrinfo` with DNS), `net/if.h` |
| Users and passwords | `pwd.h`, `grp.h`, `shadow.h`, `crypt.h` (SHA-crypt, `$5$`) |
| Patterns | `regex.h` (POSIX basic and extended), `fnmatch.h` |
| Random numbers | `sys/random.h` (`getrandom`), `stdlib.h` (`arc4random`) |
| The system log | `syslog.h` (to the kernel log) |
| ginnOS devices | `sys/fb.h`, `sys/input.h`, `sys/kbd.h`, `sys/soundcard.h`, `sys/reboot.h` |
| Other | `getopt` in `unistd.h`, `locale.h`, `langinfo.h`, `sys/utsname.h`, `dlfcn.h` |

## How it works

- **System calls.** Each wrapper calls the kernel through
  `int 0x80` (i686) or `syscall` (x86_64), in
  [`src/libc/arch/`](https://github.com/lassedtu/ginnOS/tree/main/src/libc/arch).
  A negative result sets `errno` and returns -1.
- **`errno`** is per thread.
- **`malloc`** cuts chunks from one heap that grows with `sbrk`. Free
  chunks are on lists by size. A lock makes it safe for threads.
- **stdio** streams have a lock, so two threads can write to one stream.
- **Threads** start with `clone`. Each thread has one mapping: a guard
  page, its stack, and its thread-local storage at the top. Mutexes,
  condition variables and semaphores sleep on a futex.
- **Math** uses the x87 FPU. `printf("%f")` and `strtod` round
  correctly (with the code from musl).
- **Time** is UTC. There are no time zones, and `localtime` is the same
  as `gmtime`.
- **The locale** is `"C"` only. The wide-character functions use UTF-8.

## What it does not have

| Missing | What to do |
| --- | --- |
| shared libraries, `dlopen` | Link statically. `dlopen` always fails, and `dlerror` says why. |
| `glob`, `wordexp` | Use `opendir` and `fnmatch`. |
| `getopt_long` | Use `getopt`. |
| `posix_spawn` | Use `fork` and `execvp`. |
| `iconv` | Text is UTF-8. |
| `epoll`, `kqueue` | Use `poll`. |
| `shm_open`, `sem_open`, System V IPC | Use `shm_create` and unnamed semaphores. |
| `statvfs`, `getifaddrs` | None. |
| time zones, locales other than `"C"` | None. |
| `complex.h`, `fenv.h` | None. |
| C++ standard library | None. See [Limits](toolchain.md#limits). |

If a program needs a missing function, add it to libc with a test in
[`src/user/libctest/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/libctest).
Type `libctest` in ginnOS to run these tests. `malloc` and `crypt` also
have tests that run on the build computer with `make host-test`
([`src/libc/test/`](https://github.com/lassedtu/ginnOS/tree/main/src/libc/test)).
