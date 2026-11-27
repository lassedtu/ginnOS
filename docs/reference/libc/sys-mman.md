<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/mman.h&gt;

Source: [`src/libc/include/sys/mman.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/mman.h)

memory-mapping interface (userspace).

the prot/flag values here are ABI and must match the kernel side in
src/kernel/memory/mman.h exactly.

## mmap

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
```

map memory into the calling process's address space.

two forms are supported:
  - anonymous (flags has MAP\_ANONYMOUS, fd &lt; 0): zero-filled pages.
  - device-backed (fd is an open device node such as "/dev/fb0"): maps the
    device's memory (e.g. the linear framebuffer).
with MAP\_FIXED the mapping goes exactly at @p addr (page-aligned),
replacing anything mapped there; otherwise @p addr is a hint the kernel
uses if that range is free, else it picks a free range in its mmap region.

- `addr`: address (MAP\_FIXED) or hint (may be NULL).
- `length`: number of bytes to map (rounded up to a page).
- `prot`: protection bits (PROT\_\*).
- `flags`: mapping flags (MAP\_\*).
- `fd`: device fd for a device-backed mapping, or -1 for anonymous.
- `offset`: byte offset into the mapping (must be page-aligned; 0 today).

Returns: the mapped address on success, or MAP\_FAILED on error (errno set).

## munmap

```c
int munmap(void *addr, size_t length);
```

remove the mappings in \[addr, addr + length).

- `addr`: page-aligned start.
- `length`: number of bytes (rounded up to a page).

Returns: 0 on success, -1 on error (errno set).

## mprotect

```c
int mprotect(void *addr, size_t length, int prot);
```

change the protection of \[addr, addr + length), which must be mapped.

- `addr`: page-aligned start.
- `length`: number of bytes (rounded up to a page).
- `prot`: the new PROT\_\* bits.

Returns: 0 on success, -1 on error (errno set; ENOMEM if part of the range is not mapped).

## shm\_create

```c
int shm_create(size_t size, int fd_flags);
```

make a shared memory object (a ginnOS call, like Linux's memfd\_create):
size bytes of zeroed memory, rounded up to whole pages. every mmap of
the returned fd (MAP\_SHARED), in this process or another one that has
the fd (through fork, or sent over a socket), maps the same memory.

- `size`: the size in bytes.
- `fd_flags`: FD\_CLOEXEC or 0.

Returns: the fd (open read-write), or -1 with errno set.
