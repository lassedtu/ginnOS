<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;pthread.h&gt;

Source: [`src/libc/include/pthread.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/pthread.h)

POSIX threads: start, wait for and end threads; mutexes,
       condition variables, read-write locks, barriers, spin locks,
       one-time initialization and thread-specific data.

each thread is a kernel thread (clone) with its own stack and
thread-local storage. the locks sleep on futexes, which are private to
the process: PTHREAD\_PROCESS\_SHARED is not supported. there is no
cancellation.

the functions return 0 or an error number; they do not set errno.
every object of these types is valid when it is all zero bytes, so the
static initializers are {0}.

This header has no doc comments yet.
