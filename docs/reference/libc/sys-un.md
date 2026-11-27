<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/un.h&gt;

Source: [`src/libc/include/sys/un.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/un.h)

the address of a Unix domain socket.

the layout is ABI and must match ksockaddr\_un\_t in
src/kernel/ipc/unix\_socket.h. the names are kept in a kernel table, not
as files: any path works, and nothing shows up in the directory.

This header has no doc comments yet.
