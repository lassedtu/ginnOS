<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;stdio.h&gt;

Source: [`src/libc/include/stdio.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/stdio.h)

buffered streams and formatted input/output.

a FILE buffers the bytes of one file descriptor. a stream on a terminal
is written out at the end of every stdio call (so printf and write
calls stay in order); other streams are written out when the buffer is
full, by fflush, by fclose, and by exit (not by \_exit).

## freopen

```c
FILE *freopen(const char *path, const char *mode, FILE *stream);
```

give a stream another file: the old file is closed, the stream keeps
its fd number (so freopen on stdin or stdout changes fd 0 or 1).

- `path`: the new file; NULL keeps the file and changes only the mode.

Returns: stream, or NULL (errno set; the stream is closed).

## tmpfile

```c
FILE *tmpfile(void);
```

a new file for reading and writing that goes when it is closed (it is
made in /tmp and removed at once).

## popen

```c
FILE *popen(const char *command, const char *mode);
```

run "sh -c command" with a pipe to its standard output ("r") or from
its standard input ("w").

Returns: the stream of the pipe, or NULL (errno set).

## pclose, setvbuf, setbuf, fileno

```c
int pclose(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);
int fileno(FILE *stream);
```

close a stream of popen and wait for the command.

Returns: its status as waitpid gives it, or -1.

## printf, fprintf, dprintf, sprintf, snprintf, vprintf, vfprintf, vdprintf, vsprintf, vsnprintf

```c
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *stream, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vdprintf(int fd, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
```

formatted output. conversions: d i u o x X c s p n f F e E g G %;
flags - + space # 0; a width and a precision (also as \*); lengths hh h
l ll z j t L.

## sscanf

```c
int sscanf(const char *str, const char *fmt, ...);
```

formatted input from a string: %d %i %u %x %o %c %s %\[...] %n and %%,
with widths and the lengths hh h l ll z.

Returns: the number of fields stored, or EOF at the end of the input.

## rename, renameat

```c
int rename(const char *old_path, const char *new_path);
int renameat(int old_dirfd, const char *old_path, int new_dirfd, const char *new_path);
```

rename a file or directory (on the same filesystem).

Returns: 0, or -1 (errno set; EXDEV across filesystems).

## remove

```c
int remove(const char *path);
```

remove a file or an empty directory.

## perror

```c
void perror(const char *s);
```

write "s: " (when s is not NULL or empty) and the text of errno to
stderr (in errno.c; errno.h declares it too).

## getline

```c
ssize_t getline(char **line, size_t *size, FILE *stream);
```

read a line, with its newline, into \*line (made larger with realloc
as it needs; \*line may be NULL and \*size 0 at first).

Returns: the bytes read, or -1 at the end of the file or for an error.

## getdelim

```c
ssize_t getdelim(char **line, size_t *size, int delimiter, FILE *stream);
```

as getline, up to and with delimiter instead of a newline

## fmemopen

```c
FILE *fmemopen(void *memory, size_t size, const char *mode);
```

a stream on size bytes of memory (or on new memory, freed by fclose,
when memory is NULL). reads stop at the content's end; writes stop at
size and keep a null byte after the content while there is room.
