/**
 * the input core (see input.h).
 *
 * each open of /dev/input is a reader with a ring of events. a report
 * goes into every reader's ring; a full ring drops its oldest event and
 * marks the reader, so its next read starts with SYN_DROPPED.
 *
 * locking: one spinlock (interrupts off), shared with the drivers'
 * interrupt handlers.
 */

#include "input.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/device/device.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/time/timer.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"

#define MAX_READERS 8
#define RING_EVENTS 256

typedef struct
{
    bool in_use;
    bool dropped;       // events were lost: the next read starts with SYN_DROPPED
    bool grab;          // this reader holds the keyboard
    uint32_t head;      // the next event to read
    uint32_t count;
    input_event_t ring[RING_EVENTS];
    wait_queue_t waiters;
} reader_t;

static reader_t readers[MAX_READERS];
static spinlock_t lock;
static volatile uint32_t grabs; // readers that hold the keyboard
static device_t input_device;

static void push(reader_t *reader, const input_event_t *event)
{
    if (reader->count == RING_EVENTS)
    {
        reader->head = (reader->head + 1) % RING_EVENTS;
        reader->count--;
        reader->dropped = true;
    }
    reader->ring[(reader->head + reader->count) % RING_EVENTS] = *event;
    reader->count++;
}

static void report(uint16_t type, uint16_t code, int32_t value)
{
    input_event_t event;
    event.time_ms = (uint32_t)(timer_now() * (1000 / TIMER_HZ));
    event.type = type;
    event.code = code;
    event.value = value;

    bool any = false;
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < MAX_READERS; i++)
    {
        if (readers[i].in_use)
        {
            push(&readers[i], &event);
            if (type == EV_SYN)
            {
                wait_queue_wake_all(&readers[i].waiters);
            }
            any = true;
        }
    }
    spin_unlock_irqrestore(&lock, flags);

    if (any && type == EV_SYN)
    {
        poll_notify();
    }
}

void input_report(uint16_t type, uint16_t code, int32_t value)
{
    report(type, code, value);
}

void input_sync(void)
{
    report(EV_SYN, SYN_REPORT, 0);
}

bool input_keyboard_grabbed(void)
{
    return grabs > 0;
}

/* ---- the /dev/input file ------------------------------------------------- */

static reader_t *reader_of(file_t *file)
{
    return file->device_data;
}

static int32_t input_read(file_t *file, void *buf, uint32_t count)
{
    reader_t *reader = reader_of(file);
    uint32_t wanted = count / sizeof(input_event_t);
    if (wanted == 0)
    {
        return -EINVAL; // whole events only
    }

    input_event_t *out = buf;
    uint32_t flags = spin_lock_irqsave(&lock);
    while (reader->count == 0 && !reader->dropped)
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

    // the syscall layer has checked the buffer (as for pipes); copying
    // with the lock held keeps the events in order
    uint32_t done = 0;
    if (reader->dropped)
    {
        input_event_t marker = {0, EV_SYN, SYN_DROPPED, 0};
        out[done++] = marker;
        reader->dropped = false;
    }
    while (done < wanted && reader->count > 0)
    {
        out[done++] = reader->ring[reader->head];
        reader->head = (reader->head + 1) % RING_EVENTS;
        reader->count--;
    }
    spin_unlock_irqrestore(&lock, flags);
    return (int32_t)(done * sizeof(input_event_t));
}

static uint32_t input_poll(file_t *file)
{
    reader_t *reader = reader_of(file);
    return (reader->count > 0 || reader->dropped) ? POLLIN : 0;
}

static int32_t input_ioctl(file_t *file, uint32_t request, void *arg)
{
    reader_t *reader = reader_of(file);
    if (request != INPUT_IOC_GRAB)
    {
        return -ENOTTY;
    }
    int32_t on;
    if (kerr_failed(copy_from_user(&on, arg, sizeof(on))))
    {
        return -EFAULT;
    }

    uint32_t flags = spin_lock_irqsave(&lock);
    if (on && !reader->grab)
    {
        reader->grab = true;
        grabs++;
    }
    else if (!on && reader->grab)
    {
        reader->grab = false;
        grabs--;
    }
    spin_unlock_irqrestore(&lock, flags);
    return 0;
}

static int32_t input_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0644;
    st->st_nlink = 1;
    st->st_blksize = sizeof(input_event_t);
    return 0;
}

static void input_release(file_t *file)
{
    reader_t *reader = reader_of(file);
    uint32_t flags = spin_lock_irqsave(&lock);
    if (reader->grab)
    {
        grabs--;
    }
    reader->in_use = false;
    reader->grab = false;
    spin_unlock_irqrestore(&lock, flags);
    kfree(file);
}

static const file_ops_t input_file_ops = {
    .read = input_read,
    .ioctl = input_ioctl,
    .stat = input_stat,
    .poll = input_poll,
    .release = input_release,
};

static kerr_t input_open(device_t *dev, uint32_t flags, file_t **out)
{
    (void)dev;
    if ((flags & O_ACCMODE) == O_WRONLY)
    {
        return KERR_INVAL;
    }

    file_t *file = file_new(&input_file_ops, flags);
    if (!file)
    {
        return KERR_NOMEM;
    }

    uint32_t irq_flags = spin_lock_irqsave(&lock);
    reader_t *reader = NULL;
    for (uint32_t i = 0; i < MAX_READERS; i++)
    {
        if (!readers[i].in_use)
        {
            reader = &readers[i];
            reader->in_use = true;
            reader->dropped = false;
            reader->grab = false;
            reader->head = 0;
            reader->count = 0;
            wait_queue_init(&reader->waiters);
            break;
        }
    }
    spin_unlock_irqrestore(&lock, irq_flags);

    if (!reader)
    {
        kfree(file);
        return KERR_BUSY;
    }
    file->device_data = reader;
    *out = file;
    return KERR_OK;
}

static const device_ops_t input_ops = {
    .open = input_open,
};

void input_init(void)
{
    spinlock_init(&lock);
    strncpy(input_device.name, "input", DEVICE_NAME_MAX - 1);
    input_device.name[DEVICE_NAME_MAX - 1] = '\0';
    input_device.type = DEVICE_TYPE_INPUT;
    input_device.ops = &input_ops;
    input_device.driver_data = NULL;
    device_register(&input_device);
}
