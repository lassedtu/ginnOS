#include "klog.h"

#include "drivers/serial/serial.h"
#include "common/stdio.h"
#include "common/string.h"
#include "kernel/device/device.h"

// short tag printed at the start of each line, indexed by klog_level_t.
static const char *const level_tags[] = {
    "[DEBUG] ",
    "[INFO ] ",
    "[WARN ] ",
    "[ERROR] ",
};

static void (*echo)(char c);

void klog_set_echo(void (*putchar)(char c))
{
    echo = putchar;
}

static void echo_text(const char *text)
{
    while (*text)
        echo(*text++);
}

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

    if (echo)
    {
        echo_text(level_tags[level]);
        va_start(args, fmt);
        stdio_format_args(echo, fmt, args);
        va_end(args);
        echo_text("\r\n");
    }
}

#define KMSG_LINE 200 // longer lines are cut

static uint32_t kmsg_write(device_t *dev, uint32_t offset, const void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    const char *in = buf;
    char line[KMSG_LINE + 1];
    uint32_t n = 0;
    for (uint32_t i = 0; i <= len; i++)
    {
        // a line ends at a newline or at the end of the write
        if (i == len || in[i] == '\n')
        {
            if (n > 0)
            {
                line[n] = '\0';
                klog(KLOG_LEVEL_INFO, "%s", line);
            }
            n = 0;
        }
        else if (n < KMSG_LINE && in[i] != '\r')
        {
            line[n++] = in[i];
        }
    }
    return len;
}

static const device_ops_t kmsg_ops = {
    .write = kmsg_write,
};

void klog_device_init(void)
{
    static device_t kmsg;
    strncpy(kmsg.name, "kmsg", DEVICE_NAME_MAX - 1);
    kmsg.type = DEVICE_TYPE_CHAR;
    kmsg.ops = &kmsg_ops;
    device_register(&kmsg);
}
