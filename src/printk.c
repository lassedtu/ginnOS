#include "printk.h"

#include <stdarg.h>
#include <stddef.h>

#include "vga.h"

static void printk_write_string(const char *text)
{
    if (text == NULL)
    {
        vga_write_string("(null)");
        return;
    }

    vga_write_string(text);
}

static void printk_write_char(char character)
{
    vga_write_char(character);
}

static void printk_write_number(unsigned int value, int base)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[33];
    int index = 0;

    if (value == 0)
    {
        printk_write_char('0');
        return;
    }

    while (value > 0)
    {
        buffer[index++] = digits[value % base];
        value /= base;
    }

    while (index-- > 0)
    {
        printk_write_char(buffer[index]);
    }
}

static void printk_write_signed(int value)
{
    if (value < 0)
    {
        printk_write_char('-');
        value = -value;
    }

    printk_write_number((unsigned int)value, 10);
}

static void vprintk_internal(const char *format, va_list args)
{
    if (format == NULL)
    {
        return;
    }

    for (const char *cursor = format; *cursor != '\0'; cursor++)
    {
        if (*cursor != '%')
        {
            printk_write_char(*cursor);
            continue;
        }

        cursor++;
        if (*cursor == '\0')
        {
            break;
        }

        switch (*cursor)
        {
        case 's':
            printk_write_string(va_arg(args, const char *));
            break;
        case 'd':
            printk_write_signed(va_arg(args, int));
            break;
        case 'u':
            printk_write_number(va_arg(args, unsigned int), 10);
            break;
        case 'x':
            printk_write_number(va_arg(args, unsigned int), 16);
            break;
        case 'c':
            printk_write_char((char)va_arg(args, int));
            break;
        case '%':
            printk_write_char('%');
            break;
        default:
            printk_write_char('%');
            printk_write_char(*cursor);
            break;
        }
    }
}

void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk_internal(format, args);
    va_end(args);
}

void printkernel(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk_internal(format, args);
    va_end(args);
}
