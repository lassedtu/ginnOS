<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;libgen.h&gt;

Source: [`src/libc/include/libgen.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/libgen.h)

the parts of a path. both may change path, and both may return
       a static string (".", "/").

## basename

```c
char *basename(char *path);
```

the last part: "/usr/lib/" gives "lib"

## dirname

```c
char *dirname(char *path);
```

all but the last part: "/usr/lib/" gives "/usr", "file" gives "."
