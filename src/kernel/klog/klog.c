#include "klog.h"

#include "drivers/serial/serial.h"
#include "common/stdio.h"

// short tag printed at the start of each line, indexed by klog_level_t.
static const char *const level_tags[] = {
    "[DEBUG] ",
    "[INFO ] ",
    "[WARN ] ",
    "[ERROR] ",
};

void klog_init(void)
{
    serial_initialize();
}

void klog(klog_level_t level, const char *fmt, ...)
{
    if (level < KLOG_LEVEL_DEBUG || level > KLOG_LEVEL_ERROR)
    {
        return;
    }

    serial_write(level_tags[level]);

    // reuse the stdio formatter, writing to serial
    va_list args;
    va_start(args, fmt);
    stdio_format_args(serial_putchar, fmt, args);
    va_end(args);

    serial_putchar('\n');
}
