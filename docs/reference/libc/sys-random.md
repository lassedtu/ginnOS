<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/random.h&gt;

Source: [`src/libc/include/sys/random.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/random.h)

random bytes from the kernel's generator (getrandom).

## getrandom

```c
ssize_t getrandom(void *buf, size_t length, unsigned int flags);
```

fill buf with length random bytes. waits until the kernel's generator is
seeded (early at boot only), unless GRND\_NONBLOCK or GRND\_INSECURE.

Returns: the bytes given (a long request can stop early at a signal), or -1 with errno EAGAIN, EINTR, EINVAL or EFAULT.
