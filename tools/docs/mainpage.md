# The ginnOS reference {#mainpage}

This reference comes from the comments in the ginnOS source. Doxygen
makes it each time the site is built. To change it, change the comment
in the source. The manual is at <https://lassedtu.github.io/ginnOS-docs/>.

## Where to start

| For | Look at |
| --- | --- |
| a program that uses libc | the headers in `src/libc/include/`, for example `stdio.h`, `unistd.h`, `sys/socket.h` |
| a program for the desktop | `idunn.h`, `idunn_theme.h`, `heimdall.h`, `gfx.h`, `bragi.h`, `conf.h` |
| the window server protocol | `heimdall_proto.h` |
| the system calls | `src/kernel/syscall/syscall.h` (the numbers), and the `sys_` functions in `src/kernel/syscall/` |
| the ioctls | `sys/ioctl.h`, `sys/fb.h`, `sys/kbd.h`, `sys/input.h`, `sys/soundcard.h` |
| the kernel | the headers in `src/kernel/`, `src/arch/`, `src/drivers/`, `src/fs/` and `src/common/` |

Open **Files** for the list of all headers, or use the search field.

## The architecture

Some kernel headers have one part for i686 and one for x86_64. This
reference shows the i686 part.
