#include "stdio.h"

#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "termios.h"
#include "unistd.h"

/**
 * @file stdio.c
 * @brief streams (FILE) and the printf and scanf engines.
 */

// stream flags
#define STREAM_READ  0x1
#define STREAM_WRITE 0x2
#define STREAM_EOF   0x4
#define STREAM_ERROR 0x8
#define STREAM_TTY   0x10 // written out at the end of each stdio call

struct stdio_file
{
    int fd;
    int flags;
    int mode;            // _IOFBF, _IOLBF or _IONBF
    char *buffer;
    size_t size;
    size_t write_len;    // bytes waiting to be written
    size_t read_pos;     // next byte to hand out
    size_t read_len;     // bytes read into the buffer
    int unget;           // a byte pushed back, or EOF
    int owns_buffer;
    struct stdio_file *next; // every open stream, for fflush(NULL)
    pid_t pid;               // popen: the command at the other end, else 0
    // fmemopen: the stream is this memory, not a file (fd is -1)
    char *memory;
    size_t memory_size; // the bytes it has
    size_t memory_len;  // the bytes of content (reads stop here)
    size_t memory_pos;
    int owns_memory;    // allocated by fmemopen: freed by fclose
};

static char stdin_buffer[BUFSIZ];
static char stdout_buffer[BUFSIZ];

static FILE stdin_file = {.fd = 0, .flags = STREAM_READ, .mode = _IOLBF,
                          .buffer = stdin_buffer, .size = BUFSIZ, .unget = EOF};
static FILE stdout_file = {.fd = 1, .flags = STREAM_WRITE, .mode = _IOLBF,
                           .buffer = stdout_buffer, .size = BUFSIZ, .unget = EOF};
static FILE stderr_file = {.fd = 2, .flags = STREAM_WRITE, .mode = _IONBF, .unget = EOF};

FILE *stdin = &stdin_file;
FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;

static FILE *open_streams;

/**
 * the reads, writes, seeks and closes of a stream, from its file or from
 * its memory (fmemopen).
 */
static ssize_t raw_read(FILE *stream, void *buffer, size_t n)
{
    if (!stream->memory)
        return read(stream->fd, buffer, n);
    size_t left = stream->memory_len - stream->memory_pos;
    n = n < left ? n : left;
    memcpy(buffer, stream->memory + stream->memory_pos, n);
    stream->memory_pos += n;
    return (ssize_t)n;
}

static ssize_t raw_write(FILE *stream, const void *data, size_t n)
{
    if (!stream->memory)
        return write(stream->fd, data, n);
    size_t room = stream->memory_size - stream->memory_pos;
    if (room == 0)
    {
        errno = ENOSPC;
        return -1;
    }
    n = n < room ? n : room;
    memcpy(stream->memory + stream->memory_pos, data, n);
    stream->memory_pos += n;
    if (stream->memory_pos > stream->memory_len)
        stream->memory_len = stream->memory_pos;
    // the content stays a string while there is room for the null byte
    if (stream->memory_len < stream->memory_size)
        stream->memory[stream->memory_len] = '\0';
    return (ssize_t)n;
}

static long raw_seek(FILE *stream, long offset, int whence)
{
    if (!stream->memory)
        return lseek(stream->fd, (int)offset, whence);
    long base = whence == SEEK_SET   ? 0
                : whence == SEEK_CUR ? (long)stream->memory_pos
                                     : (long)stream->memory_len;
    long position = base + offset;
    if (whence < SEEK_SET || whence > SEEK_END || position < 0 ||
        position > (long)stream->memory_size)
    {
        errno = EINVAL;
        return -1;
    }
    stream->memory_pos = (size_t)position;
    return position;
}

static int raw_close(FILE *stream)
{
    if (!stream->memory)
        return close(stream->fd);
    if (stream->owns_memory)
        free(stream->memory);
    return 0;
}
static int std_streams_ready;

/**
 * stdout on a terminal flushes after each call; to a pipe or a file it
 * keeps full buffering. decided at the first use.
 */
static void ready_std_streams(void)
{
    if (std_streams_ready)
        return;
    std_streams_ready = 1;

    stdin_file.next = &stdout_file;
    stdout_file.next = &stderr_file;
    stderr_file.next = open_streams;
    open_streams = &stdin_file;

    if (isatty(1))
        stdout_file.flags |= STREAM_TTY;
    else
        stdout_file.mode = _IOFBF;
    stderr_file.flags |= STREAM_TTY;
}

/**
 * write every buffered byte.
 */
static int flush_stream(FILE *stream)
{
    size_t done = 0;
    while (done < stream->write_len)
    {
        ssize_t n = raw_write(stream, stream->buffer + done, stream->write_len - done);
        if (n <= 0)
        {
            stream->flags |= STREAM_ERROR;
            stream->write_len = 0;
            return EOF;
        }
        done += (size_t)n;
    }
    stream->write_len = 0;
    return 0;
}

/**
 * give back unread input so the file offset matches what was consumed.
 */
static void drop_read_buffer(FILE *stream)
{
    size_t unread = stream->read_len - stream->read_pos;
    if (unread > 0)
        raw_seek(stream, -(long)unread, SEEK_CUR);
    stream->read_pos = stream->read_len = 0;
    stream->unget = EOF;
}

int fflush(FILE *stream)
{
    ready_std_streams();
    if (!stream)
    {
        int result = 0;
        for (FILE *s = open_streams; s; s = s->next)
            if (s->write_len && flush_stream(s) == EOF)
                result = EOF;
        return result;
    }
    if (stream->write_len)
        return flush_stream(stream);
    if (stream->read_len)
        drop_read_buffer(stream);
    return 0;
}

/**
 * the end of a stdio call: a terminal stream is written out now.
 */
static void end_call(FILE *stream)
{
    if ((stream->flags & STREAM_TTY) && stream->write_len)
        flush_stream(stream);
}

/**
 * add bytes to a stream's buffer (or write them at once without one).
 */
static size_t put_bytes(FILE *stream, const char *data, size_t len)
{
    if (!(stream->flags & STREAM_WRITE))
    {
        stream->flags |= STREAM_ERROR;
        errno = EBADF;
        return 0;
    }
    if (stream->read_len)
        drop_read_buffer(stream);

    if (stream->mode == _IONBF || !stream->buffer)
    {
        size_t done = 0;
        while (done < len)
        {
            ssize_t n = raw_write(stream, data + done, len - done);
            if (n <= 0)
            {
                stream->flags |= STREAM_ERROR;
                break;
            }
            done += (size_t)n;
        }
        return done;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (stream->write_len == stream->size && flush_stream(stream) == EOF)
            return i;
        stream->buffer[stream->write_len++] = data[i];
        if (stream->mode == _IOLBF && data[i] == '\n' && !(stream->flags & STREAM_TTY))
            flush_stream(stream);
    }
    return len;
}

/**
 * fill the read buffer. returns false at end of file or on an error.
 */
static int fill(FILE *stream)
{
    if (!(stream->flags & STREAM_READ))
    {
        stream->flags |= STREAM_ERROR;
        errno = EBADF;
        return 0;
    }
    // reading from a terminal shows what was written first (a prompt)
    if (stream == stdin)
        fflush(stdout);
    if (stream->write_len)
        flush_stream(stream);

    char *target = stream->buffer;
    size_t room = stream->size;
    char single;
    if (!target || stream->mode == _IONBF)
    {
        target = &single;
        room = 1;
    }

    ssize_t n = raw_read(stream, target, room);
    if (n <= 0)
    {
        stream->flags |= n == 0 ? STREAM_EOF : STREAM_ERROR;
        return 0;
    }
    if (target == &single)
    {
        stream->unget = (unsigned char)single;
        return 1;
    }
    stream->read_pos = 0;
    stream->read_len = (size_t)n;
    return 1;
}

int fgetc(FILE *stream)
{
    ready_std_streams();
    if (stream->unget != EOF)
    {
        int c = stream->unget;
        stream->unget = EOF;
        return c;
    }
    if (stream->read_pos == stream->read_len)
    {
        if (!fill(stream))
            return EOF;
        if (stream->unget != EOF)
            return fgetc(stream);
    }
    return (unsigned char)stream->buffer[stream->read_pos++];
}

int getc(FILE *stream)
{
    return fgetc(stream);
}

int getchar(void)
{
    return fgetc(stdin);
}

int ungetc(int c, FILE *stream)
{
    if (c == EOF || stream->unget != EOF)
        return EOF;
    stream->unget = (unsigned char)c;
    stream->flags &= ~STREAM_EOF;
    return (unsigned char)c;
}

char *fgets(char *buf, int size, FILE *stream)
{
    int i = 0;
    while (i < size - 1)
    {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    buf[i] = '\0';
    return buf;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    unsigned char *out = ptr;
    size_t total = size * count;
    size_t done = 0;
    while (done < total)
    {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        out[done++] = (unsigned char)c;
    }
    return size ? done / size : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    ready_std_streams();
    size_t done = put_bytes(stream, ptr, size * count);
    end_call(stream);
    return size ? done / size : 0;
}

int fputc(int c, FILE *stream)
{
    char byte = (char)c;
    ready_std_streams();
    size_t done = put_bytes(stream, &byte, 1);
    end_call(stream);
    return done == 1 ? (unsigned char)c : EOF;
}

int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *stream)
{
    size_t len = strlen(s);
    return fwrite(s, 1, len, stream) == len ? 0 : EOF;
}

int puts(const char *s)
{
    ready_std_streams();
    size_t len = strlen(s);
    size_t done = put_bytes(stdout, s, len);
    done += put_bytes(stdout, "\n", 1);
    end_call(stdout);
    return done == len + 1 ? 0 : EOF;
}

/**
 * the open() flags for an fopen mode ("r", "w", "a", with "+" and "b").
 */
static int parse_mode(const char *mode, int *stream_flags)
{
    int plus = strchr(mode, '+') != NULL;
    switch (mode[0])
    {
    case 'r':
        *stream_flags = STREAM_READ | (plus ? STREAM_WRITE : 0);
        return plus ? O_RDWR : O_RDONLY;
    case 'w':
        *stream_flags = STREAM_WRITE | (plus ? STREAM_READ : 0);
        return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    case 'a':
        *stream_flags = STREAM_WRITE | (plus ? STREAM_READ : 0);
        return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    default:
        return -1;
    }
}

FILE *fdopen(int fd, const char *mode)
{
    int flags;
    if (parse_mode(mode, &flags) < 0)
    {
        errno = EINVAL;
        return NULL;
    }

    ready_std_streams();
    FILE *stream = calloc(1, sizeof(FILE));
    char *buffer = malloc(BUFSIZ);
    if (!stream || !buffer)
    {
        free(stream);
        free(buffer);
        errno = ENOMEM;
        return NULL;
    }

    stream->fd = fd;
    stream->flags = flags;
    stream->mode = _IOFBF;
    stream->buffer = buffer;
    stream->size = BUFSIZ;
    stream->unget = EOF;
    stream->owns_buffer = 1;
    if (isatty(fd))
    {
        stream->mode = _IOLBF;
        stream->flags |= STREAM_TTY;
    }
    stream->next = open_streams;
    open_streams = stream;
    return stream;
}

FILE *fmemopen(void *memory, size_t size, const char *mode)
{
    int flags;
    if (size == 0 || parse_mode(mode, &flags) < 0)
    {
        errno = EINVAL;
        return NULL;
    }
    ready_std_streams();
    FILE *stream = calloc(1, sizeof(FILE));
    char *buffer = malloc(BUFSIZ);
    char *own = memory ? NULL : calloc(1, size);
    if (!stream || !buffer || (!memory && !own))
    {
        free(stream);
        free(buffer);
        free(own);
        errno = ENOMEM;
        return NULL;
    }
    stream->fd = -1;
    stream->flags = flags;
    stream->mode = _IOFBF;
    stream->buffer = buffer;
    stream->size = BUFSIZ;
    stream->unget = EOF;
    stream->owns_buffer = 1;
    stream->memory = memory ? memory : own;
    stream->owns_memory = own != NULL;
    stream->memory_size = size;
    // "r": all of it is content; "w": none (an empty string); "a": up to
    // the first null byte, where writing starts
    if (mode[0] == 'r')
        stream->memory_len = size;
    else if (mode[0] == 'w')
        stream->memory[0] = '\0';
    else
    {
        char *end = memchr(stream->memory, '\0', size);
        stream->memory_len = end ? (size_t)(end - stream->memory) : size;
        stream->memory_pos = stream->memory_len;
    }
    stream->next = open_streams;
    open_streams = stream;
    return stream;
}

FILE *fopen(const char *path, const char *mode)
{
    int stream_flags;
    int flags = parse_mode(mode, &stream_flags);
    if (flags < 0)
    {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(path, flags | O_CLOEXEC, 0666);
    if (fd < 0)
        return NULL;

    FILE *stream = fdopen(fd, mode);
    if (!stream)
        close(fd);
    return stream;
}

int fclose(FILE *stream)
{
    int result = fflush(stream);

    for (FILE **link = &open_streams; *link; link = &(*link)->next)
    {
        if (*link == stream)
        {
            *link = stream->next;
            break;
        }
    }

    if (raw_close(stream) < 0)
        result = EOF;
    if (stream == stdin || stream == stdout || stream == stderr)
        return result;
    if (stream->owns_buffer)
        free(stream->buffer);
    free(stream);
    return result;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    int stream_flags;
    int flags = parse_mode(mode, &stream_flags);
    if (flags < 0)
    {
        fclose(stream);
        errno = EINVAL;
        return NULL;
    }
    fflush(stream);
    if (path)
    {
        int fd = open(path, flags | O_CLOEXEC, 0666);
        if (fd < 0)
        {
            int error = errno;
            fclose(stream);
            errno = error;
            return NULL;
        }
        // keep the fd number: freopen on stdin must change fd 0
        if (fd != stream->fd)
        {
            if (dup2(fd, stream->fd) < 0)
            {
                int error = errno;
                close(fd);
                fclose(stream);
                errno = error;
                return NULL;
            }
            close(fd);
        }
    }
    stream->flags = stream_flags;
    stream->read_pos = stream->read_len = stream->write_len = 0;
    stream->unget = EOF;
    if (isatty(stream->fd))
    {
        stream->flags |= STREAM_TTY;
        stream->mode = _IOLBF;
    }
    else if (stream->buffer)
    {
        stream->mode = _IOFBF;
    }
    return stream;
}

FILE *tmpfile(void)
{
    char path[] = "/tmp/tmpfileXXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return NULL;
    unlink(path); // the file stays until its last close
    FILE *stream = fdopen(fd, "w+");
    if (!stream)
        close(fd);
    return stream;
}

FILE *popen(const char *command, const char *mode)
{
    int reading = mode && mode[0] == 'r';
    if (!command || !mode || (!reading && mode[0] != 'w'))
    {
        errno = EINVAL;
        return NULL;
    }
    int ends[2];
    if (pipe(ends) < 0)
        return NULL;
    // the command's end: the write end when we read its output
    int theirs = reading ? ends[1] : ends[0];
    int ours = reading ? ends[0] : ends[1];

    pid_t child = fork();
    if (child < 0)
    {
        close(ends[0]);
        close(ends[1]);
        return NULL;
    }
    if (child == 0)
    {
        int target = reading ? 1 : 0;
        close(ours);
        if (theirs != target)
        {
            dup2(theirs, target);
            close(theirs);
        }
        // the pipes of earlier popen calls are not the command's
        for (FILE *s = open_streams; s; s = s->next)
            if (s->pid)
                close(s->fd);
        char *argv[] = {"sh", "-c", (char *)command, NULL};
        execve("/bin/sh", argv, environ);
        _exit(127);
    }

    close(theirs);
    fcntl(ours, F_SETFD, FD_CLOEXEC); // not into other programs we start
    FILE *stream = fdopen(ours, reading ? "r" : "w");
    if (!stream)
    {
        close(ours);
        waitpid(child, NULL, 0);
        return NULL;
    }
    stream->pid = child;
    return stream;
}

int pclose(FILE *stream)
{
    pid_t pid = stream->pid;
    if (!pid)
    {
        errno = ECHILD;
        return -1;
    }
    fclose(stream); // the command sees the end of its input
    int status;
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
            return -1;
    }
    return status;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)
        return -1;
    fflush(stream);
    if (buf && size > 0)
    {
        if (stream->owns_buffer)
            free(stream->buffer);
        stream->buffer = buf;
        stream->size = size;
        stream->owns_buffer = 0;
    }
    stream->mode = mode;
    return 0;
}

void setbuf(FILE *stream, char *buf)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

int fileno(FILE *stream)
{
    return stream->fd;
}

int fseek(FILE *stream, long offset, int whence)
{
    fflush(stream);
    stream->unget = EOF;
    if (raw_seek(stream, offset, whence) < 0)
        return -1;
    stream->flags &= ~STREAM_EOF;
    return 0;
}

long ftell(FILE *stream)
{
    long position = raw_seek(stream, 0, SEEK_CUR);
    if (position < 0)
        return -1;
    position += (long)stream->write_len;
    position -= (long)(stream->read_len - stream->read_pos);
    if (stream->unget != EOF)
        position--;
    return position;
}

int fseeko(FILE *stream, off_t offset, int whence)
{
    return fseek(stream, (long)offset, whence);
}

off_t ftello(FILE *stream)
{
    return (off_t)ftell(stream);
}

void rewind(FILE *stream)
{
    fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}

int feof(FILE *stream)
{
    return (stream->flags & STREAM_EOF) != 0;
}

int ferror(FILE *stream)
{
    return (stream->flags & STREAM_ERROR) != 0;
}

void clearerr(FILE *stream)
{
    stream->flags &= ~(STREAM_EOF | STREAM_ERROR);
}

void flockfile(FILE *stream)
{
    (void)stream;
}

int ftrylockfile(FILE *stream)
{
    (void)stream;
    return 0;
}

void funlockfile(FILE *stream)
{
    (void)stream;
}

int getc_unlocked(FILE *stream)
{
    return getc(stream);
}

int getchar_unlocked(void)
{
    return getchar();
}

int putc_unlocked(int c, FILE *stream)
{
    return putc(c, stream);
}

int putchar_unlocked(int c)
{
    return putchar(c);
}

int remove(const char *path)
{
    if (unlink(path) == 0)
        return 0;
    return rmdir(path);
}

/*
 * the printf engine
 */

/**
 * where formatted output goes: a stream, a buffer, or a file descriptor.
 */
typedef struct
{
    FILE *stream;      // a stream, or NULL
    char *buf;         // a buffer, or NULL (snprintf)
    size_t size;       // buffer size
    int fd;            // a descriptor (dprintf), or -1
    char pending[128]; // bytes for fd, written in pieces
    size_t pending_len;
    size_t count;      // characters produced (also past the buffer's end)
} out_t;

static void emit(out_t *out, const char *data, size_t len)
{
    if (out->stream)
    {
        put_bytes(out->stream, data, len);
    }
    else if (out->fd >= 0)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (out->pending_len == sizeof(out->pending))
            {
                write(out->fd, out->pending, out->pending_len);
                out->pending_len = 0;
            }
            out->pending[out->pending_len++] = data[i];
        }
    }
    else
    {
        for (size_t i = 0; i < len; i++)
            if (out->buf && out->count + i < out->size - 1)
                out->buf[out->count + i] = data[i];
    }
    out->count += len;
}

static void emit_repeat(out_t *out, char c, int times)
{
    for (int i = 0; i < times; i++)
        emit(out, &c, 1);
}

// conversion flags
#define FLAG_LEFT  0x01 // -
#define FLAG_PLUS  0x02 // +
#define FLAG_SPACE 0x04 // space
#define FLAG_ALT   0x08 // #
#define FLAG_ZERO  0x10 // 0

/**
 * divide by 10 a 64-bit number (no __udivdi3 without libgcc).
 */
static unsigned long long div_small(unsigned long long value, unsigned int divisor,
                                    unsigned int *remainder)
{
    unsigned long long quotient = 0;
    unsigned long long rest = 0;
    for (int bit = 63; bit >= 0; bit--)
    {
        rest = (rest << 1) | ((value >> bit) & 1ull);
        if (rest >= divisor)
        {
            rest -= divisor;
            quotient |= 1ull << bit;
        }
    }
    *remainder = (unsigned int)rest;
    return quotient;
}

/**
 * write a number with its sign, prefix, precision, width and padding.
 * @param digits the digits (no sign).
 */
static void emit_number(out_t *out, const char *digits, int len, const char *prefix, int flags,
                        int width, int precision)
{
    int zeros = precision > len ? precision - len : 0;
    if (precision == 0 && len == 1 && digits[0] == '0')
        len = 0; // "%.0d" of 0 prints nothing
    int prefix_len = (int)strlen(prefix);
    int total = prefix_len + zeros + len;

    if ((flags & FLAG_ZERO) && !(flags & FLAG_LEFT) && precision < 0 && width > total)
    {
        zeros += width - total;
        total = width;
    }
    if (!(flags & FLAG_LEFT))
        emit_repeat(out, ' ', width - total);
    emit(out, prefix, (size_t)prefix_len);
    emit_repeat(out, '0', zeros);
    emit(out, digits, (size_t)len);
    if (flags & FLAG_LEFT)
        emit_repeat(out, ' ', width - total);
}

static int integer_digits(char *buf, unsigned long long value, unsigned int base, int upper)
{
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[24];
    int len = 0;
    do
    {
        unsigned int digit;
        value = div_small(value, base, &digit);
        reversed[len++] = set[digit];
    } while (value);
    for (int i = 0; i < len; i++)
        buf[i] = reversed[len - 1 - i];
    return len;
}

/**
 * %f, %e and %g. digits come from 32-bit pieces, as the conversions of
 * double to 64-bit integers need libgcc.
 */
static void emit_float(out_t *out, double value, char conversion, int flags, int width,
                       int precision)
{
    char text[64];
    int len = 0;
    char sign[2] = {0, 0};

    if (value < 0 || (value == 0 && 1 / value < 0))
    {
        sign[0] = '-';
        value = -value;
    }
    else if (flags & FLAG_PLUS)
        sign[0] = '+';
    else if (flags & FLAG_SPACE)
        sign[0] = ' ';

    int upper = conversion == 'F' || conversion == 'E' || conversion == 'G';
    if (value != value)
    {
        emit_number(out, upper ? "NAN" : "nan", 3, sign, flags & FLAG_LEFT, width, -1);
        return;
    }
    if (value > 1.7976931348623157e308)
    {
        emit_number(out, upper ? "INF" : "inf", 3, sign, flags & FLAG_LEFT, width, -1);
        return;
    }

    if (precision < 0)
        precision = 6;
    if (precision > 17)
        precision = 17;

    char style = (char)(conversion | 0x20); // f, e or g
    int exponent = 0;
    if (style != 'f')
    {
        double scaled = value;
        if (scaled != 0)
        {
            while (scaled >= 10)
            {
                scaled /= 10;
                exponent++;
            }
            while (scaled < 1)
            {
                scaled *= 10;
                exponent--;
            }
        }
        if (style == 'g')
        {
            int p = precision ? precision : 1;
            if (exponent < -4 || exponent >= p)
            {
                style = 'e';
                precision = p - 1;
            }
            else
            {
                style = 'f';
                precision = p - 1 - exponent;
            }
        }
        if (style == 'e')
            value = scaled;
    }

    // round, then split into whole and fractional parts
    double rounding = 0.5;
    for (int i = 0; i < precision; i++)
        rounding /= 10;
    value += rounding;
    if (style == 'e' && value >= 10)
    {
        value /= 10;
        exponent++;
    }

    if (value >= 1e18)
    {
        // too big for the 32-bit pieces: print it in e style
        emit_float(out, sign[0] == '-' ? -value : value, upper ? 'E' : 'e', flags, width,
                   precision);
        return;
    }
    unsigned int high = (unsigned int)(value / 1e9);
    double low_part = value - (double)high * 1e9;
    unsigned int low = (unsigned int)low_part;
    double fraction = low_part - low;

    if (high)
    {
        len += integer_digits(text + len, high, 10, 0);
        char piece[12];
        int piece_len = integer_digits(piece, low, 10, 0);
        for (int i = piece_len; i < 9; i++)
            text[len++] = '0';
        memcpy(text + len, piece, (size_t)piece_len);
        len += piece_len;
    }
    else
    {
        len += integer_digits(text + len, low, 10, 0);
    }

    if (precision > 0 || (flags & FLAG_ALT))
        text[len++] = '.';
    for (int i = 0; i < precision; i++)
    {
        fraction *= 10;
        int digit = (int)fraction;
        text[len++] = (char)('0' + digit);
        fraction -= digit;
    }

    // %g drops trailing zeros
    if ((conversion | 0x20) == 'g' && !(flags & FLAG_ALT) && memchr(text, '.', (size_t)len))
    {
        while (text[len - 1] == '0')
            len--;
        if (text[len - 1] == '.')
            len--;
    }

    if (style == 'e')
    {
        text[len++] = upper ? 'E' : 'e';
        text[len++] = exponent < 0 ? '-' : '+';
        int e = exponent < 0 ? -exponent : exponent;
        if (e < 10)
            text[len++] = '0';
        len += integer_digits(text + len, (unsigned int)e, 10, 0);
    }

    emit_number(out, text, len, sign, flags, width, -1);
}

/**
 * the printf engine for every output kind.
 */
static int format(out_t *out, const char *fmt, va_list ap)
{
    while (*fmt)
    {
        if (*fmt != '%')
        {
            const char *start = fmt;
            while (*fmt && *fmt != '%')
                fmt++;
            emit(out, start, (size_t)(fmt - start));
            continue;
        }
        fmt++;

        int flags = 0;
        for (;; fmt++)
        {
            if (*fmt == '-')
                flags |= FLAG_LEFT;
            else if (*fmt == '+')
                flags |= FLAG_PLUS;
            else if (*fmt == ' ')
                flags |= FLAG_SPACE;
            else if (*fmt == '#')
                flags |= FLAG_ALT;
            else if (*fmt == '0')
                flags |= FLAG_ZERO;
            else
                break;
        }

        int width = 0;
        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            if (width < 0)
            {
                flags |= FLAG_LEFT;
                width = -width;
            }
            fmt++;
        }
        else
        {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        int precision = -1;
        if (*fmt == '.')
        {
            fmt++;
            precision = 0;
            if (*fmt == '*')
            {
                precision = va_arg(ap, int);
                fmt++;
            }
            else
            {
                while (*fmt >= '0' && *fmt <= '9')
                    precision = precision * 10 + (*fmt++ - '0');
            }
        }

        // length: 0 int, 1 long, 2 long long, -1 short, -2 char
        int length = 0;
        if (*fmt == 'h')
        {
            length = fmt[1] == 'h' ? -2 : -1;
            fmt += fmt[1] == 'h' ? 2 : 1;
        }
        else if (*fmt == 'l')
        {
            length = fmt[1] == 'l' ? 2 : 1;
            fmt += fmt[1] == 'l' ? 2 : 1;
        }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't' || *fmt == 'L')
        {
            length = *fmt == 'j' ? 2 : 1;
            fmt++;
        }

        char digits[32];
        char conversion = *fmt;
        if (conversion == '\0')
            break;
        fmt++;

        switch (conversion)
        {
        case 'd':
        case 'i':
        {
            long long value = length == 2 ? va_arg(ap, long long)
                              : length == 1 ? va_arg(ap, long)
                                            : va_arg(ap, int);
            if (length == -1)
                value = (short)value;
            else if (length == -2)
                value = (signed char)value;
            const char *sign = value < 0 ? "-" : (flags & FLAG_PLUS) ? "+"
                                             : (flags & FLAG_SPACE) ? " " : "";
            unsigned long long magnitude =
                value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
            int len = integer_digits(digits, magnitude, 10, 0);
            emit_number(out, digits, len, sign, flags, width, precision);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'p':
        {
            unsigned long long value;
            if (conversion == 'p')
                value = (unsigned long)va_arg(ap, void *);
            else
                value = length == 2 ? va_arg(ap, unsigned long long)
                        : length == 1 ? va_arg(ap, unsigned long)
                                      : va_arg(ap, unsigned int);
            if (length == -1)
                value = (unsigned short)value;
            else if (length == -2)
                value = (unsigned char)value;

            unsigned int base = conversion == 'u' ? 10 : conversion == 'o' ? 8 : 16;
            int len = integer_digits(digits, value, base, conversion == 'X');
            const char *prefix = "";
            if (conversion == 'p' || ((flags & FLAG_ALT) && value != 0 && base == 16))
                prefix = conversion == 'X' ? "0X" : "0x";
            else if ((flags & FLAG_ALT) && base == 8 && digits[0] != '0')
                prefix = "0";
            emit_number(out, digits, len, prefix, flags, width, precision);
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(ap, int);
            emit_number(out, &c, 1, "", flags & FLAG_LEFT, width, -1);
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = 0;
            while (s[len] && (precision < 0 || len < precision))
                len++;
            if (!(flags & FLAG_LEFT))
                emit_repeat(out, ' ', width - len);
            emit(out, s, (size_t)len);
            if (flags & FLAG_LEFT)
                emit_repeat(out, ' ', width - len);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            emit_float(out, va_arg(ap, double), conversion, flags, width, precision);
            break;
        case 'n':
            *va_arg(ap, int *) = (int)out->count;
            break;
        case '%':
            emit(out, "%", 1);
            break;
        default:
            emit(out, "%", 1);
            emit(out, &conversion, 1);
            break;
        }
    }
    return (int)out->count;
}

static void init_out(out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->fd = -1;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    ready_std_streams();
    out_t out;
    init_out(&out);
    out.stream = stream;
    int count = format(&out, fmt, ap);
    end_call(stream);
    return ferror(stream) ? -1 : count;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int vdprintf(int fd, const char *fmt, va_list ap)
{
    out_t out;
    init_out(&out);
    out.fd = fd;
    int count = format(&out, fmt, ap);
    if (out.pending_len)
        write(fd, out.pending, out.pending_len);
    return count;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t out;
    init_out(&out);
    out.buf = size ? buf : NULL;
    out.size = size;
    int count = format(&out, fmt, ap);
    if (size)
        buf[out.count < size ? out.count : size - 1] = '\0';
    return count;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1 >> 1, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int count = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return count;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int count = vfprintf(stream, fmt, ap);
    va_end(ap);
    return count;
}

int dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int count = vdprintf(fd, fmt, ap);
    va_end(ap);
    return count;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int count = vsprintf(buf, fmt, ap);
    va_end(ap);
    return count;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int count = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return count;
}

/*
 * sscanf
 */

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 99;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const char *in = str;
    int stored = 0;

    while (*fmt)
    {
        if (is_space(*fmt))
        {
            while (is_space(*in))
                in++;
            fmt++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%')
        {
            if (*fmt == '%')
                fmt++;
            if (*in != *fmt)
                break;
            in++;
            fmt++;
            continue;
        }
        fmt++;

        int suppress = *fmt == '*';
        if (suppress)
            fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int length = 0;
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z')
        {
            length += *fmt == 'l' ? 1 : *fmt == 'h' ? -1 : 1;
            fmt++;
        }
        char conversion = *fmt++;
        if (width == 0)
            width = 1 << 30;

        if (conversion == 'n')
        {
            if (!suppress)
                *va_arg(ap, int *) = (int)(in - str);
            continue;
        }
        if (conversion != 'c' && conversion != '[')
            while (is_space(*in))
                in++;
        if (*in == '\0')
        {
            if (stored == 0)
                stored = EOF;
            break;
        }

        if (conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'x' ||
            conversion == 'X' || conversion == 'o')
        {
            int base = conversion == 'x' || conversion == 'X' ? 16 : conversion == 'o' ? 8 : 10;
            int negative = 0;
            if ((*in == '-' || *in == '+') && width > 0)
            {
                negative = *in == '-';
                in++;
                width--;
            }
            if (conversion == 'i' && in[0] == '0')
                base = (in[1] == 'x' || in[1] == 'X') ? 16 : 8;
            if (base == 16 && in[0] == '0' && (in[1] == 'x' || in[1] == 'X') && width > 2)
            {
                in += 2;
                width -= 2;
            }
            const char *start = in;
            unsigned long long value = 0;
            while (width > 0 && digit_value(*in) < base)
            {
                value = value * (unsigned int)base + (unsigned int)digit_value(*in++);
                width--;
            }
            if (in == start)
                break;
            if (negative)
                value = (unsigned long long)-(long long)value;
            if (!suppress)
            {
                if (length >= 2)
                    *va_arg(ap, long long *) = (long long)value;
                else if (length == 1)
                    *va_arg(ap, long *) = (long)value;
                else if (length == -1)
                    *va_arg(ap, short *) = (short)value;
                else if (length <= -2)
                    *va_arg(ap, signed char *) = (signed char)value;
                else
                    *va_arg(ap, int *) = (int)value;
                stored++;
            }
        }
        else if (conversion == 'c')
        {
            if (width == 1 << 30)
                width = 1;
            char *out = suppress ? NULL : va_arg(ap, char *);
            for (; width > 0 && *in; width--)
            {
                if (out)
                    *out++ = *in;
                in++;
            }
            if (!suppress)
                stored++;
        }
        else if (conversion == 's' || conversion == '[')
        {
            // a %[...] set: which characters belong to the field
            char set[256] = {0};
            int negate = 0;
            if (conversion == '[')
            {
                if (*fmt == '^')
                {
                    negate = 1;
                    fmt++;
                }
                if (*fmt == ']')
                    set[(unsigned char)*fmt++] = 1;
                while (*fmt && *fmt != ']')
                    set[(unsigned char)*fmt++] = 1;
                if (*fmt == ']')
                    fmt++;
            }
            char *out = suppress ? NULL : va_arg(ap, char *);
            const char *start = in;
            while (width > 0 && *in)
            {
                int member = conversion == 's' ? !is_space(*in)
                                               : (set[(unsigned char)*in] != 0) != negate;
                if (!member)
                    break;
                if (out)
                    *out++ = *in;
                in++;
                width--;
            }
            if (in == start)
                break;
            if (out)
            {
                *out = '\0';
                stored++;
            }
        }
        else
        {
            break;
        }
    }

    va_end(ap);
    return stored;
}

ssize_t getdelim(char **line, size_t *size, int delimiter, FILE *stream)
{
    if (!line || !size || !stream)
    {
        errno = EINVAL;
        return -1;
    }
    size_t length = 0;
    for (;;)
    {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        // room for this byte and the null byte
        if (length + 2 > *size || !*line)
        {
            size_t bigger = *size < 64 ? 128 : *size * 2;
            char *grown = realloc(*line, bigger);
            if (!grown)
            {
                errno = ENOMEM;
                return -1;
            }
            *line = grown;
            *size = bigger;
        }
        (*line)[length++] = (char)c;
        if (c == delimiter)
            break;
    }
    if (length == 0)
        return -1;
    (*line)[length] = '\0';
    return (ssize_t)length;
}

ssize_t getline(char **line, size_t *size, FILE *stream)
{
    return getdelim(line, size, '\n', stream);
}
