# Testing

ginnOS has three kinds of tests: kernel tests that run in a test kernel,
host tests that run the libraries on the build computer, and test
programs that run in ginnOS. For the commands, see
[Build ginnOS](../develop/building.md#test).

| Kind | Where | Run with |
| --- | --- | --- |
| kernel tests | [`src/test/`](https://github.com/lassedtu/ginnOS/tree/main/src/test) | `make test` |
| host tests | `src/lib/<name>/test/`, `src/user/<name>/test/`, `src/libc/test/` | `make host-test` |
| test programs | `src/user/*test/` | type the name in ginnOS, for example `libctest` |

## Kernel tests

`make test` compiles the kernel again with `-DKERNEL_TEST` and the tests
in `src/test/`, into its own object tree and its own disk image. The
normal kernel has no test code. The test kernel boots in QEMU without a
window, mounts the root file system, and runs all tests in place of
`/bin/init`. The results go to the serial port. At the end, the kernel
writes to QEMU's `isa-debug-exit` device, so `make test` stops with the
result as its exit status.

A test:

```c
#include "ktest.h"

KTEST(heap_alloc_and_free)
{
    void *p = kmalloc(64);
    KEXPECT(p != NULL);
    kfree(p);
}
```

`KTEST(name)` registers the test in the `.ktests` section of the
kernel, so a new file in `src/test/` needs no list. `KEXPECT(cond)` and
`KEXPECT_EQ(a, b)` record a failure, and the test continues. One run
then shows all failures, not only the first.

A test that hangs stops after 120 seconds. Change this with
`make test TEST_TIMEOUT=<seconds>`.

## Host tests

The libraries do not use the kernel, so they also build for the build
computer. Each library with tests has a `test/` folder with a Makefile:

```make
TEST     := conf
SRCS     := conf_test.c
LIB_SRCS := ../conf.c
include ../../host-test.mk
```

| Variable | What it is |
| --- | --- |
| `TEST` | the name of the test program |
| `SRCS` | the test sources, in `test/` |
| `LIB_SRCS` | the library sources to test |
| `LIBS` | other libraries that the test needs |
| `EXTRA_CFLAGS` | more compiler flags |

`make host-test` finds each such Makefile. The host compiler builds the
tests with the undefined behaviour sanitizer. To add the address
sanitizer, use `make host-test SANITIZE=address,undefined`.

The checks are in
[`hosttest.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/hosttest/hosttest.h):
`CHECK(cond)` and `CHECK_EQ(a, b)`. A failed check is recorded, and the
test continues.

### Golden images

A test of drawing code compares its picture with a golden image, a PPM
file in `test/golden/`. The test also writes its picture to the current
folder, so you can look at it. After an intended change to the look,
run `make host-test GOLDEN=update`, look at the new golden images, and
commit them.

## Test programs

These programs test the kernel through its system calls, as a program
sees it. They are in the image. Run them in ginnOS:

| Program | What it tests |
| --- | --- |
| `libctest` | libc: files, POSIX calls, threads, the network, regular expressions, wide characters, random numbers |
| `proctest` | processes: `fork`, `exec`, `wait` |
| `threadtest`, `pthreadtest` | threads: `clone`, `exit_group`, and the pthreads of libc |
| `sigtest` | signals |
| `fstest` | the file system calls |
| `memtest` | user memory: `brk`, `mmap`, page faults |
| `shmtest` | shared memory |
| `sockettest` | Unix sockets |
| `ptytest` | pseudo-terminals |
| `evtest`, `audiotest` | the input events, and the sound card |

## Before a commit

1. Run `make test`, and `make host-test` if you changed a library or an
   app.
2. Build for i686 and for x86_64 (`make ARCH=x86_64`), with no warnings.
3. Start the image with `make run`, and try what you changed.
