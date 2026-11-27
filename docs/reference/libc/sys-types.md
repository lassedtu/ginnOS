<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/types.h&gt;

Source: [`src/libc/include/sys/types.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/types.h)

basic system types. the other headers take their types from here,
so each type has one definition.

the sizes that the kernel ABI fixes (struct stat, struct timespec) are the
same on i686 and x86\_64: the ids, modes and file offsets are 32 bits;
time\_t and clock\_t are long (the width of the kernel's long).

This header has no doc comments yet.
