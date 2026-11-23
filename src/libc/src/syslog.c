/**
 * @file syslog.c
 * @brief the system log, written to the kernel log (/dev/kmsg): "name[pid]:
 *        message", one write each, so the lines of programs do not mix.
 */

#include "syslog.h"

#include "errno.h"
#include "stdbool.h"
#include "fcntl.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"

#define LINE 512

static struct
{
    char ident[32];
    int options;
    int mask; // the levels written
    int fd;   // /dev/kmsg, or -1 before the first message
} L = {"", 0, 0xff, -1};

void openlog(const char *ident, int options, int facility)
{
    (void)facility;
    snprintf(L.ident, sizeof(L.ident), "%s", ident ? ident : "");
    L.options = options;
    if (options & LOG_NDELAY && L.fd < 0)
        L.fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
}

void closelog(void)
{
    if (L.fd >= 0)
        close(L.fd);
    L.fd = -1;
}

int setlogmask(int mask)
{
    int old = L.mask;
    if (mask)
        L.mask = mask;
    return old;
}

void vsyslog(int priority, const char *format, va_list args)
{
    if (!(L.mask & LOG_MASK(LOG_PRI(priority))))
        return;
    int saved = errno; // %m in syslog is strerror(errno): ours keeps errno for the caller
    char line[LINE];
    int n = 0;
    if (L.ident[0])
        n = L.options & LOG_PID ? snprintf(line, sizeof(line), "%s[%d]: ", L.ident, getpid())
                                : snprintf(line, sizeof(line), "%s: ", L.ident);
    errno = saved;
    vsnprintf(line + n, sizeof(line) - (size_t)n - 1, format, args);
    size_t length = strlen(line);
    if (length == 0 || line[length - 1] != '\n')
    {
        line[length++] = '\n';
        line[length] = '\0';
    }
    if (L.fd < 0)
        L.fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    bool written = L.fd >= 0 && write(L.fd, line, length) == (ssize_t)length;
    if (L.options & LOG_PERROR || (!written && L.options & LOG_CONS))
        write(STDERR_FILENO, line, length);
}

void syslog(int priority, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsyslog(priority, format, args);
    va_end(args);
}
