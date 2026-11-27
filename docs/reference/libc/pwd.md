<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;pwd.h&gt;

Source: [`src/libc/include/pwd.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/pwd.h)

the user database (/etc/passwd: name:password:uid:gid:gecos:home:shell).

## getpwuid, getpwnam

```c
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
```

look up a user by id or name. the result is overwritten by the next call.

Returns: the entry, or NULL if there is none.
