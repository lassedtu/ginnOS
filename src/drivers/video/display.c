/**
 * @file display.c
 * @brief the display layer (see display.h) and /dev/display.
 *
 * each open of /dev/display is a reader with a small ring of fb_event_t.
 * a driver's interrupt only marks that the size the screen wants may have
 * changed; the driver is asked for it in process context (a read or a
 * poll), as asking can wait for the device.
 */

#include "display.h"

#include "common/fb/fb_ioctl.h"
#include "common/memory.h"
#include "common/string.h"
#include "kernel/device/device.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"

#define MAX_READERS 4
#define RING_EVENTS 8

typedef struct
{
    uint32_t head;
    uint32_t count;
    fb_event_t ring[RING_EVENTS];
    wait_queue_t waiters;
} reader_t;

static const display_driver_t *driver;
static reader_t *readers[MAX_READERS];
static spinlock_t lock;
static device_t display_device;

// the size the screen wants, as last asked; 0 if not known
static uint32_t wanted_width;
static uint32_t wanted_height;
static volatile bool ask_again; // an interrupt said it may have changed

// common sizes offered after the preferred one
static const struct
{
    uint16_t width;
    uint16_t height;
} common_sizes[] = {
    {1024, 768},  {1280, 720},  {1280, 800},  {1366, 768},  {1440, 900},  {1600, 900},
    {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {2560, 1664},
    {2880, 1800}, {3840, 2160},
};

static void push(reader_t *reader, const fb_event_t *event)
{
    if (reader->count == RING_EVENTS)
    {
        // full: the oldest goes (the newest says where things are now)
        reader->head = (reader->head + 1) % RING_EVENTS;
        reader->count--;
    }
    reader->ring[(reader->head + reader->count) % RING_EVENTS] = *event;
    reader->count++;
}

void display_event(uint32_t type, uint32_t width, uint32_t height)
{
    fb_event_t event = {type, width, height};
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < MAX_READERS; i++)
    {
        if (readers[i])
        {
            push(readers[i], &event);
            wait_queue_wake_all(&readers[i]->waiters);
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    poll_notify();
}

/**
 * ask the driver for the size the screen wants, if an interrupt said it
 * may have changed, and tell the readers when it did.
 */
static void ask_driver(void)
{
    if (!ask_again || !driver || !driver->preferred)
    {
        return;
    }
    ask_again = false;
    uint32_t width;
    uint32_t height;
    if (!driver->preferred(&width, &height))
    {
        return;
    }
    if (width != wanted_width || height != wanted_height)
    {
        wanted_width = width;
        wanted_height = height;
        display_event(FB_EVENT_PREFERRED, width, height);
    }
}

void display_preferred_changed(void)
{
    ask_again = true;
    // the pollers come back and ask (display_poll)
    poll_notify();
}

const display_driver_t *display_driver(void)
{
    return driver;
}

void display_attach(const display_driver_t *d)
{
    driver = d;
    ask_again = true;
    ask_driver();
}

static bool fits(uint32_t width, uint32_t height)
{
    return driver && width <= driver->max_width && height <= driver->max_height;
}

bool display_mode_at(uint32_t index, uint32_t *width, uint32_t *height, bool *preferred)
{
    if (!driver)
    {
        return false;
    }
    ask_driver();
    bool have = wanted_width && fits(wanted_width, wanted_height);
    if (have && index == 0)
    {
        *width = wanted_width;
        *height = wanted_height;
        *preferred = true;
        return true;
    }
    uint32_t n = have ? 1 : 0;
    for (uint32_t i = 0; i < sizeof(common_sizes) / sizeof(common_sizes[0]); i++)
    {
        uint32_t w = common_sizes[i].width;
        uint32_t h = common_sizes[i].height;
        if (!fits(w, h) || (have && w == wanted_width && h == wanted_height))
        {
            continue;
        }
        if (n++ == index)
        {
            *width = w;
            *height = h;
            *preferred = false;
            return true;
        }
    }
    return false;
}

static reader_t *reader_of(file_t *file)
{
    return file->device_data;
}

static int32_t display_read(file_t *file, void *buf, uint32_t count)
{
    reader_t *reader = reader_of(file);
    uint32_t wanted = count / sizeof(fb_event_t);
    if (wanted == 0)
    {
        return -EINVAL; // whole events only
    }
    ask_driver();

    fb_event_t *out = buf;
    uint32_t flags = spin_lock_irqsave(&lock);
    while (reader->count == 0)
    {
        if (file->flags & O_NONBLOCK)
        {
            spin_unlock_irqrestore(&lock, flags);
            return -EAGAIN;
        }
        if (signal_pending(process_current()))
        {
            spin_unlock_irqrestore(&lock, flags);
            return -ERESTARTSYS;
        }
        wait_queue_block_locked_interruptible(&reader->waiters, &lock);
    }
    uint32_t done = 0;
    while (done < wanted && reader->count > 0)
    {
        out[done++] = reader->ring[reader->head];
        reader->head = (reader->head + 1) % RING_EVENTS;
        reader->count--;
    }
    spin_unlock_irqrestore(&lock, flags);
    return (int32_t)(done * sizeof(fb_event_t));
}

static uint32_t display_poll(file_t *file)
{
    ask_driver();
    return reader_of(file)->count > 0 ? POLLIN : 0;
}

static int32_t display_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0444;
    st->st_nlink = 1;
    st->st_blksize = sizeof(fb_event_t);
    return 0;
}

static void display_release(file_t *file)
{
    reader_t *reader = reader_of(file);
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < MAX_READERS; i++)
    {
        if (readers[i] == reader)
        {
            readers[i] = NULL;
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    kfree(reader);
    kfree(file);
}

static const file_ops_t display_file_ops = {
    .read = display_read,
    .stat = display_stat,
    .poll = display_poll,
    .release = display_release,
};

static kerr_t display_open(device_t *dev, uint32_t flags, file_t **out)
{
    (void)dev;
    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        return KERR_INVAL;
    }
    file_t *file = file_new(&display_file_ops, flags);
    reader_t *reader = kmalloc(sizeof(*reader));
    if (!file || !reader)
    {
        kfree(file);
        kfree(reader);
        return KERR_NOMEM;
    }
    memset(reader, 0, sizeof(*reader));
    wait_queue_init(&reader->waiters);

    bool added = false;
    uint32_t irq_flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < MAX_READERS && !added; i++)
    {
        if (!readers[i])
        {
            readers[i] = reader;
            added = true;
        }
    }
    spin_unlock_irqrestore(&lock, irq_flags);
    if (!added)
    {
        kfree(reader);
        kfree(file);
        return KERR_BUSY;
    }
    file->device_data = reader;
    *out = file;
    return KERR_OK;
}

static const device_ops_t display_ops = {
    .open = display_open,
};

void display_init(void)
{
    spinlock_init(&lock);
    strncpy(display_device.name, "display", DEVICE_NAME_MAX - 1);
    display_device.name[DEVICE_NAME_MAX - 1] = '\0';
    display_device.type = DEVICE_TYPE_CHAR;
    display_device.ops = &display_ops;
    device_register(&display_device);
}
