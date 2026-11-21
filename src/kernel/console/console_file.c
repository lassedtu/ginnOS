/**
 * the console as an open-file description (kernel/vfs/file.h): reads come
 * from the keyboard, writes go to the screen. there is one description,
 * shared by every process's stdio; it is never released.
 */

#include "console.h"

#include "drivers/keyboard/keyboard.h"
#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/vfs/file.h"
#include "common/memory.h"

/**
 * raw mode: one keyboard_event_t per read.
 */
static int32_t read_event(char *buf, uint32_t count)
{
    if (count < sizeof(keyboard_event_t))
    {
        return -EINVAL;
    }

    keyboard_event_t event;
    keyboard_wait_event(&event);
    memcpy(buf, &event, sizeof(event));
    return (int32_t)sizeof(event);
}

/**
 * cooked mode: a line with echo and backspace, up to count bytes; the
 * newline ends it and is stored.
 */
static int32_t read_line(char *buf, uint32_t count)
{
    uint32_t i = 0;
    while (i < count)
    {
        char c = keyboard_read(); // blocks until a key is available

        if (c == '\n')
        {
            console_putchar('\r');
            console_putchar('\n');
            buf[i++] = '\n';
            break;
        }

        if (c == '\b')
        {
            if (i > 0)
            {
                i--;
                console_putchar('\b');
            }
            continue;
        }

        buf[i++] = c;
        console_putchar(c);
    }
    return (int32_t)i;
}

static int32_t console_file_read(file_t *file, void *buf, uint32_t count)
{
    (void)file;
    process_t *proc = process_current();
    if (proc && proc->tty_raw)
    {
        return read_event(buf, count);
    }
    return read_line(buf, count);
}

static int32_t console_file_write(file_t *file, const void *buf, uint32_t count)
{
    (void)file;
    // one batched console update per write: the caret is placed and the
    // display flushed once at the end, not after every byte
    console_write_bytes(buf, count);
    return (int32_t)count;
}

static int32_t console_file_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0620;
    st->st_nlink = 1;
    st->st_blksize = 1024;
    return 0;
}

static const file_ops_t console_file_ops = {
    .read = console_file_read,
    .write = console_file_write,
    .stat = console_file_stat,
};

// the reference it starts with is never dropped, so it is never released
static file_t console_file = {
    .ops = &console_file_ops,
    .refcount = 1,
    .flags = O_RDWR,
};

file_t *file_console(void)
{
    file_ref(&console_file);
    return &console_file;
}
