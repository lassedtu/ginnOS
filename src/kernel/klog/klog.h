#pragma once

/**
 * kernel logging: level-tagged diagnostics written to the serial port.
 *
 * klog output goes to COM1 (see drivers/serial), so it is visible headlessly
 * under QEMU with `-serial stdio` and works before/without the VGA console.
 *
 * use the level macros (KLOG_DEBUG/INFO/WARN/ERROR) rather than calling
 * klog() directly. each expands to nothing when its level is below the
 * compile-time KLOG_LEVEL threshold, so verbose logging costs zero in
 * release builds.
 */

#include "common/stdint.h"

/**
 * log severity levels, lowest to highest.
 */
typedef enum
{
    KLOG_LEVEL_DEBUG = 0,
    KLOG_LEVEL_INFO,
    KLOG_LEVEL_WARN,
    KLOG_LEVEL_ERROR,
} klog_level_t;

/**
 * compile-time minimum level. messages below this are dropped at compile
 * time. defaults to INFO in release (NDEBUG) and DEBUG otherwise. override
 * by defining KLOG_LEVEL on the compiler command line.
 */
#ifndef KLOG_LEVEL
#ifdef NDEBUG
#define KLOG_LEVEL KLOG_LEVEL_INFO
#else
#define KLOG_LEVEL KLOG_LEVEL_DEBUG
#endif
#endif

/**
 * initialize the logging backend (brings up the serial port).
 * call once, early in boot, before any KLOG_* macro is used.
 */
void klog_init(void);

/**
 * write a level-tagged, formatted log line to the serial port.
 * appends a trailing newline. prefer the KLOG_* macros over this.
 * @param level severity of the message.
 * @param fmt   printf-style format string.
 */
void klog(klog_level_t level, const char *fmt, ...);

// level macros: expand to a klog() call only when the level is enabled,
// otherwise to a no-op that discards the arguments at compile time.
#if KLOG_LEVEL <= KLOG_LEVEL_DEBUG
#define KLOG_DEBUG(...) klog(KLOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#define KLOG_DEBUG(...) ((void)0)
#endif

#if KLOG_LEVEL <= KLOG_LEVEL_INFO
#define KLOG_INFO(...) klog(KLOG_LEVEL_INFO, __VA_ARGS__)
#else
#define KLOG_INFO(...) ((void)0)
#endif

#if KLOG_LEVEL <= KLOG_LEVEL_WARN
#define KLOG_WARN(...) klog(KLOG_LEVEL_WARN, __VA_ARGS__)
#else
#define KLOG_WARN(...) ((void)0)
#endif

#if KLOG_LEVEL <= KLOG_LEVEL_ERROR
#define KLOG_ERROR(...) klog(KLOG_LEVEL_ERROR, __VA_ARGS__)
#else
#define KLOG_ERROR(...) ((void)0)
#endif
