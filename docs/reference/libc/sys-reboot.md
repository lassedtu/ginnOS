<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/reboot.h&gt;

Source: [`src/libc/include/sys/reboot.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/reboot.h)

restart or turn off the machine (root only), as on Linux.

## reboot

```c
int reboot(int how);
```

restart or turn off the machine.

- `how`: RB\_AUTOBOOT or RB\_POWER\_OFF.

Returns: does not return on success; -1 with errno EPERM for other users, EINVAL for another value.
