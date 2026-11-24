#pragma once

/**
 * @file syslog.h
 * @brief the system log: ginnOS writes each message to the kernel log
 *        (/dev/kmsg), with the name, the pid and the level before it.
 */

#include <stdarg.h>

// levels
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

// facilities (kept in the message, not used)
#define LOG_KERN     (0 << 3)
#define LOG_USER     (1 << 3)
#define LOG_MAIL     (2 << 3)
#define LOG_DAEMON   (3 << 3)
#define LOG_AUTH     (4 << 3)
#define LOG_SYSLOG   (5 << 3)
#define LOG_LPR      (6 << 3)
#define LOG_NEWS     (7 << 3)
#define LOG_UUCP     (8 << 3)
#define LOG_CRON     (9 << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_LOCAL0   (16 << 3)
#define LOG_LOCAL1   (17 << 3)
#define LOG_LOCAL2   (18 << 3)
#define LOG_LOCAL3   (19 << 3)
#define LOG_LOCAL4   (20 << 3)
#define LOG_LOCAL5   (21 << 3)
#define LOG_LOCAL6   (22 << 3)
#define LOG_LOCAL7   (23 << 3)

#define LOG_PRIMASK     7
#define LOG_PRI(p)      ((p) & LOG_PRIMASK)
#define LOG_MASK(level) (1 << (level))
#define LOG_UPTO(level) ((1 << ((level) + 1)) - 1)
#define LOG_FACMASK     0x03f8
#define LOG_FAC(p)      (((p) & LOG_FACMASK) >> 3)
#define LOG_MAKEPRI(facility, level) ((facility) | (level))
#define LOG_NFACILITIES 24

#ifdef SYSLOG_NAMES
// the names of the levels and the facilities, for tools such as logger.
// a program that defines SYSLOG_NAMES before this header gets a copy.
typedef struct
{
    char *c_name;
    int c_val;
} CODE;

__attribute__((unused)) static CODE prioritynames[] = {
    {"alert", LOG_ALERT},     {"crit", LOG_CRIT},     {"debug", LOG_DEBUG},
    {"emerg", LOG_EMERG},     {"err", LOG_ERR},       {"error", LOG_ERR},
    {"info", LOG_INFO},       {"notice", LOG_NOTICE}, {"panic", LOG_EMERG},
    {"warn", LOG_WARNING},    {"warning", LOG_WARNING}, {0, -1},
};

__attribute__((unused)) static CODE facilitynames[] = {
    {"auth", LOG_AUTH},     {"authpriv", LOG_AUTHPRIV}, {"cron", LOG_CRON},
    {"daemon", LOG_DAEMON}, {"kern", LOG_KERN},         {"lpr", LOG_LPR},
    {"mail", LOG_MAIL},     {"news", LOG_NEWS},         {"security", LOG_AUTH},
    {"syslog", LOG_SYSLOG}, {"user", LOG_USER},         {"uucp", LOG_UUCP},
    {"local0", LOG_LOCAL0}, {"local1", LOG_LOCAL1},     {"local2", LOG_LOCAL2},
    {"local3", LOG_LOCAL3}, {"local4", LOG_LOCAL4},     {"local5", LOG_LOCAL5},
    {"local6", LOG_LOCAL6}, {"local7", LOG_LOCAL7},     {0, -1},
};
#endif

// openlog options
#define LOG_PID    0x01 // the pid after the name
#define LOG_CONS   0x02 // to the console too when the log cannot be written
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20 // to stderr too

void openlog(const char *ident, int options, int facility);
void syslog(int priority, const char *format, ...) __attribute__((format(printf, 2, 3)));
void vsyslog(int priority, const char *format, va_list args);
void closelog(void);
/** the levels to write (LOG_MASK bits); returns the old mask (0: no change) */
int setlogmask(int mask);
