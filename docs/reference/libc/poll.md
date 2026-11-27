<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;poll.h&gt;

Source: [`src/libc/include/poll.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/poll.h)

waiting for file descriptors to become ready.

the event bits and struct pollfd are ABI and must match the kernel side
in src/kernel/vfs/poll.h and sys\_io.c.

## poll

```c
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

wait until one of a set of descriptors is ready.

- `fds`: the descriptors (at most 64).
- `nfds`: the number of entries.
- `timeout`: milliseconds to wait; negative waits without limit, 0 does not wait.

Returns: the number of entries with revents set, 0 on timeout, or -1 (errno set).
