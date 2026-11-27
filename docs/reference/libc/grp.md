<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;grp.h&gt;

Source: [`src/libc/include/grp.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/grp.h)

the group database (/etc/group: name:password:gid:members).

## getgrgid, getgrnam

```c
struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
```

look up a group by id or name. the result is overwritten by the next call.

Returns: the entry, or NULL if there is none.
