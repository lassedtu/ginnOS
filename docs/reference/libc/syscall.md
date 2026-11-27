<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;syscall.h&gt;

Source: [`src/libc/include/syscall.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/syscall.h)

This file contains the declarations of system call numbers and the raw syscall interface.

## \_syscall

```c
long _syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5);
```

the raw syscall (src/libc/arch/&lt;arch&gt;/syscall.asm). the arguments and
the result are pointer-wide, so a pointer passes through unchanged. a
result from -4095 to -1 is a negative errno.
