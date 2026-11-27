<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/utsname.h&gt;

Source: [`src/libc/include/sys/utsname.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/utsname.h)

the name of the system: uname.

## uname

```c
int uname(struct utsname *name);
```

fill name with the system's names.

Returns: 0, or -1 with errno set.
