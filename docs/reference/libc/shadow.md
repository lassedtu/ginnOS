<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;shadow.h&gt;

Source: [`src/libc/include/shadow.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/shadow.h)

the password hashes (/etc/shadow: name:hash:last:min:max:warn:
       inactive:expire:), which only root can read.

ginnOS uses the name and the hash; the other fields are kept as they
are. a hash of "\*" or "!" (or one that starts with "!") is a locked
account; an empty one needs no password.

## getspnam

```c
struct spwd *getspnam(const char *name);
```

the entry of a user, in a static buffer.

Returns: NULL if there is none, or the file cannot be read (not root).
