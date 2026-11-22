#pragma once

#include <sys/types.h>

/**
 * @file stdio.h
 * @brief buffered streams and formatted input/output.
 *
 * a FILE buffers the bytes of one file descriptor. a stream on a terminal
 * is written out at the end of every stdio call (so printf and write
 * calls stay in order); other streams are written out when the buffer is
 * full, by fflush, by fclose, and by exit (not by _exit).
 */

#ifndef NULL
#define NULL ((void *)0)
#endif

#include <stdarg.h>

typedef struct stdio_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)
#define BUFSIZ 1024

// setvbuf modes
#define _IOFBF 0 // full buffering
#define _IOLBF 1 // line buffering
#define _IONBF 2 // no buffering

// fseek whence
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

// opening and closing
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream); // NULL flushes every stream

/**
 * give a stream another file: the old file is closed, the stream keeps
 * its fd number (so freopen on stdin or stdout changes fd 0 or 1).
 * @param path the new file; NULL keeps the file and changes only the mode.
 * @return stream, or NULL (errno set; the stream is closed).
 */
FILE *freopen(const char *path, const char *mode, FILE *stream);

/**
 * a new file for reading and writing that goes when it is closed (it is
 * made in /tmp and removed at once).
 */
FILE *tmpfile(void);

/**
 * run "sh -c command" with a pipe to its standard output ("r") or from
 * its standard input ("w").
 * @return the stream of the pipe, or NULL (errno set).
 */
FILE *popen(const char *command, const char *mode);

/**
 * close a stream of popen and wait for the command.
 * @return its status as waitpid gives it, or -1.
 */
int pclose(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);
int fileno(FILE *stream);

// bytes and lines
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);
char *fgets(char *buf, int size, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int puts(const char *s);

// position and state
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fseeko(FILE *stream, off_t offset, int whence);
off_t ftello(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/**
 * formatted output. conversions: d i u o x X c s p n f F e E g G %;
 * flags - + space # 0; a width and a precision (also as *); lengths hh h
 * l ll z j t L.
 */
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

/**
 * formatted input from a string: %d %i %u %x %o %c %s %[...] %n and %%,
 * with widths and the lengths hh h l ll z.
 * @return the number of fields stored, or EOF at the end of the input.
 */
int sscanf(const char *str, const char *fmt, ...);

/**
 * rename a file or directory (on the same filesystem).
 * @return 0, or -1 (errno set; EXDEV across filesystems).
 */
int rename(const char *old_path, const char *new_path);

/**
 * remove a file or an empty directory.
 */
int remove(const char *path);

/**
 * write "s: " (when s is not NULL or empty) and the text of errno to
 * stderr (in errno.c; errno.h declares it too).
 */
void perror(const char *s);

// locks of a stream for threads. a ginnOS process has one thread, so
// they do nothing and the _unlocked calls are the plain ones; they are
// here for programs written for threads.
void flockfile(FILE *stream);
int ftrylockfile(FILE *stream); // 0: locked
void funlockfile(FILE *stream);
int getc_unlocked(FILE *stream);
int getchar_unlocked(void);
int putc_unlocked(int c, FILE *stream);
int putchar_unlocked(int c);
