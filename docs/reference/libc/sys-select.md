<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/select.h&gt;

Source: [`src/libc/include/sys/select.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/select.h)

select(), built on poll().

## select

```c
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);
```

wait until a descriptor in one of the sets is ready. on return each set
holds only its ready descriptors.

- `nfds`: one more than the highest descriptor in the sets.
- `readfds`: descriptors to watch for reading (may be NULL).
- `writefds`: descriptors to watch for writing (may be NULL).
- `exceptfds`: descriptors to watch for errors (may be NULL).
- `timeout`: how long to wait (NULL: no limit).

Returns: the number of ready descriptors in all sets, 0 on timeout, or -1 (errno set).
