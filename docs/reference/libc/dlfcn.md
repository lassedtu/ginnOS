<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;dlfcn.h&gt;

Source: [`src/libc/include/dlfcn.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/dlfcn.h)

loading shared libraries at run time. ginnOS programs are static
       and there is no dynamic loader yet: dlopen always fails, and
       dlerror says why. programs that can do without a library (SDL's
       optional parts) go on without it.

## dlopen

```c
void *dlopen(const char *file, int mode);
```

Returns: NULL: there is no dynamic loader

## dlsym

```c
void *dlsym(void *handle, const char *name);
```

Returns: NULL

## dlclose

```c
int dlclose(void *handle);
```

Returns: 0

## dlerror

```c
char *dlerror(void);
```

Returns: why the last call failed, once (then NULL)
