/**
 * pseudo-terminals (see pty.h).
 *
 * each pair has two rings: input (master to slave, after the line
 * discipline) and output (slave to master, after output processing; echo
 * goes there too). in canonical mode, typed bytes collect in an edit
 * buffer; a newline or VEOF moves the line into the input ring and adds
 * its length to a queue of lines, and a read takes at most one line. a
 * line of length 0 in the queue is an end of file.
 *
 * lifetime: the master's file and every open of the slave hold the pair.
 * when the master closes, the slave reads the end of file, its writes get
 * EIO, and the session gets SIGHUP. the pair is freed when both sides are
 * closed. the devices of /dev/pts/N are a static table, so devfs can hold
 * one while the pair goes away.
 *
 * locking: one spinlock for all pairs. user memory is copied in and out
 * through a buffer on the stack, outside the lock.
 */

#include "pty.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/console/termios.h"
#include "kernel/device/device.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/process/process.h"
#include "kernel/signal/signal.h"
#include "kernel/sync/spinlock.h"
#include "kernel/sync/wait_queue.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"
#include "kernel/vfs/stat.h"

#define RING_SIZE  4096
#define LINE_MAX   1024 // the longest line in canonical mode
#define MAX_LINES  64   // complete lines waiting to be read
#define CHUNK      256  // bytes copied per step

#define CTRL(key) ((key) & 0x1f)

typedef struct
{
    uint8_t data[RING_SIZE];
    uint32_t head;
    uint32_t count;
} ring_t;

typedef struct pty
{
    uint32_t index;
    bool master_open;
    uint32_t slave_opens;
    bool slave_was_open; // the master reads EIO after the last slave closes
    bool locked;         // the slave cannot be opened until unlockpt

    struct ktermios termios;
    struct kwinsize size;
    uint32_t session;    // the session it controls (0: none)
    uint32_t foreground; // the foreground process group

    ring_t input;
    uint16_t lines[MAX_LINES]; // canonical: the lengths of the lines in input
    uint32_t line_head;
    uint32_t line_count;
    uint8_t edit[LINE_MAX];    // canonical: the line being typed
    uint32_t edit_length;

    ring_t output;
    wait_queue_t waiters; // readers and writers of both sides
} pty_t;

static spinlock_t lock;
static pty_t *pairs[PTY_MAX];
static device_t slave_devices[PTY_MAX];
static device_t ptmx_device;

static const file_ops_t master_ops;
static const file_ops_t slave_ops;

static uint32_t ring_room(const ring_t *ring)
{
    return RING_SIZE - ring->count;
}

static void ring_put(ring_t *ring, uint8_t c)
{
    ring->data[(ring->head + ring->count) % RING_SIZE] = c;
    ring->count++;
}

static uint32_t ring_take(ring_t *ring, uint8_t *out, uint32_t count)
{
    uint32_t n = count < ring->count ? count : ring->count;
    for (uint32_t i = 0; i < n; i++)
    {
        out[i] = ring->data[(ring->head + i) % RING_SIZE];
    }
    ring->head = (ring->head + n) % RING_SIZE;
    ring->count -= n;
    return n;
}

static bool canonical(const pty_t *p)
{
    return (p->termios.c_lflag & ICANON) != 0;
}

/**
 * put one byte into the output, with output processing (ONLCR).
 * @return false if there is no room (the byte is not put).
 */
static bool put_output(pty_t *p, uint8_t c)
{
    bool crlf = c == '\n' && (p->termios.c_oflag & (OPOST | ONLCR)) == (OPOST | ONLCR);
    if (ring_room(&p->output) < (crlf ? 2u : 1u))
    {
        return false;
    }
    if (crlf)
    {
        ring_put(&p->output, '\r');
    }
    ring_put(&p->output, c);
    return true;
}

static void echo(pty_t *p, uint8_t c)
{
    if (p->termios.c_lflag & ECHO)
    {
        put_output(p, c); // an echo that does not fit is lost
    }
}

/**
 * move the edit buffer into the input as a complete line.
 */
static void commit_line(pty_t *p)
{
    for (uint32_t i = 0; i < p->edit_length; i++)
    {
        ring_put(&p->input, p->edit[i]);
    }
    p->lines[(p->line_head + p->line_count) % MAX_LINES] = (uint16_t)p->edit_length;
    p->line_count++;
    p->edit_length = 0;
}

static void flush_input(pty_t *p)
{
    p->input.head = 0;
    p->input.count = 0;
    p->line_head = 0;
    p->line_count = 0;
    p->edit_length = 0;
}

/**
 * true if the line discipline has room for one more typed byte.
 */
static bool input_room(const pty_t *p)
{
    if (canonical(p))
    {
        // the edit buffer, plus a newline, must fit in the ring later
        return p->edit_length < LINE_MAX - 1 && p->line_count < MAX_LINES &&
               p->input.count + p->edit_length + 1 <= RING_SIZE;
    }
    return ring_room(&p->input) > 0;
}

/**
 * the signal a typed byte sends with ISIG, or 0.
 */
static int signal_for(const pty_t *p, uint8_t c)
{
    if (!(p->termios.c_lflag & ISIG) || c == 0)
    {
        return 0;
    }
    if (c == p->termios.c_cc[VINTR])
        return SIGINT;
    if (c == p->termios.c_cc[VQUIT])
        return SIGQUIT;
    if (c == p->termios.c_cc[VSUSP])
        return SIGTSTP;
    return 0;
}

/**
 * one byte from the master through the line discipline. the caller has
 * checked input_room.
 * @return a signal to send to the foreground group, or 0.
 */
static int type_byte(pty_t *p, uint8_t c)
{
    const struct ktermios *t = &p->termios;

    if (c == '\r')
    {
        if (t->c_iflag & IGNCR)
            return 0;
        if (t->c_iflag & ICRNL)
            c = '\n';
    }
    else if (c == '\n' && (t->c_iflag & INLCR))
    {
        c = '\r';
    }

    int sig = signal_for(p, c);
    if (sig != 0)
    {
        flush_input(p);
        echo(p, '^');
        echo(p, (uint8_t)(c + '@'));
        echo(p, '\n');
        return sig;
    }

    if (!canonical(p))
    {
        ring_put(&p->input, c);
        echo(p, c);
        return 0;
    }

    if (c == '\n')
    {
        p->edit[p->edit_length++] = c;
        echo(p, c);
        commit_line(p);
    }
    else if (c == t->c_cc[VEOF])
    {
        commit_line(p); // an empty line is the end of file
    }
    else if (c == t->c_cc[VERASE] || c == 0x7f || c == '\b')
    {
        if (p->edit_length > 0)
        {
            p->edit_length--;
            echo(p, '\b');
            if (t->c_lflag & ECHOE)
            {
                echo(p, ' ');
                echo(p, '\b');
            }
        }
    }
    else if (c == t->c_cc[VKILL])
    {
        while (p->edit_length > 0)
        {
            p->edit_length--;
            echo(p, '\b');
            echo(p, ' ');
            echo(p, '\b');
        }
    }
    else
    {
        p->edit[p->edit_length++] = c;
        echo(p, c);
    }
    return 0;
}

/**
 * new settings. switching canonical mode off makes the bytes typed so far
 * readable; switching it on makes what is unread one line.
 */
static void set_termios(pty_t *p, const struct ktermios *next)
{
    bool was = canonical(p);
    p->termios = *next;
    bool now = canonical(p);
    if (was && !now)
    {
        for (uint32_t i = 0; i < p->edit_length && ring_room(&p->input) > 0; i++)
        {
            ring_put(&p->input, p->edit[i]);
        }
        p->edit_length = 0;
        p->line_head = 0;
        p->line_count = 0;
    }
    else if (!was && now)
    {
        p->line_head = 0;
        p->line_count = 0;
        if (p->input.count > 0)
        {
            p->lines[0] = (uint16_t)p->input.count;
            p->line_count = 1;
        }
    }
}

/**
 * true if a slave read would not wait.
 */
static bool slave_readable(const pty_t *p)
{
    if (!p->master_open)
    {
        return true;
    }
    return canonical(p) ? p->line_count > 0 : p->input.count > 0;
}

static void default_termios(struct ktermios *t)
{
    memset(t, 0, sizeof(*t));
    t->c_iflag = ICRNL;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    t->c_cc[VINTR] = CTRL('c');
    t->c_cc[VQUIT] = CTRL('\\');
    t->c_cc[VERASE] = 0x7f;
    t->c_cc[VKILL] = CTRL('u');
    t->c_cc[VEOF] = CTRL('d');
    t->c_cc[VSUSP] = CTRL('z');
    t->c_cc[VMIN] = 1;
}

/**
 * free a pair when neither side is open. call with the lock held.
 * @return the pair to free after the lock, or NULL.
 */
static pty_t *unhook_if_unused(pty_t *p)
{
    if (p->master_open || p->slave_opens > 0)
    {
        return NULL;
    }
    pairs[p->index] = NULL;
    return p;
}

static pty_t *pty_of(file_t *file)
{
    return file->device_data;
}

/**
 * wait on a pair with the lock held.
 * @return 0 to look again, or -EAGAIN / -ERESTARTSYS.
 */
static int32_t wait_on(pty_t *p, file_t *file)
{
    if (file->flags & O_NONBLOCK)
    {
        return -EAGAIN;
    }
    if (signal_pending(process_current()))
    {
        return -ERESTARTSYS;
    }
    wait_queue_block_locked_interruptible(&p->waiters, &lock);
    return 0;
}

static bool signal_refused(const process_t *proc, int sig)
{
    return proc->handlers->actions[sig].handler == SIG_IGN ||
           (proc->signals.blocked & SIG_BIT(sig));
}

static int32_t set_window_size(pty_t *p, const void *arg)
{
    struct kwinsize size;
    if (kerr_failed(copy_from_user(&size, arg, sizeof(size))))
    {
        return -EFAULT;
    }
    uint32_t flags = spin_lock_irqsave(&lock);
    bool changed = memcmp(&size, &p->size, sizeof(size)) != 0;
    p->size = size;
    uint32_t group = p->foreground;
    spin_unlock_irqrestore(&lock, flags);
    if (changed && group != 0)
    {
        signal_send_group(group, SIGWINCH);
    }
    return 0;
}

static int32_t common_ioctl(pty_t *p, uint32_t request, void *arg)
{
    switch (request)
    {
    case TCGETS:
    {
        struct ktermios t;
        uint32_t flags = spin_lock_irqsave(&lock);
        t = p->termios;
        spin_unlock_irqrestore(&lock, flags);
        return kerr_to_errno(copy_to_user(arg, &t, sizeof(t)));
    }

    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    {
        struct ktermios next;
        if (kerr_failed(copy_from_user(&next, arg, sizeof(next))))
        {
            return -EFAULT;
        }
        next.c_lflag &= ~(uint32_t)(XKEYEVENTS | XKEYRAW); // console only
        uint32_t flags = spin_lock_irqsave(&lock);
        if (request == TCSETSF)
        {
            flush_input(p);
        }
        set_termios(p, &next);
        wait_queue_wake_all(&p->waiters);
        spin_unlock_irqrestore(&lock, flags);
        poll_notify();
        return 0;
    }

    case TIOCGWINSZ:
    {
        struct kwinsize size;
        uint32_t flags = spin_lock_irqsave(&lock);
        size = p->size;
        spin_unlock_irqrestore(&lock, flags);
        return kerr_to_errno(copy_to_user(arg, &size, sizeof(size)));
    }

    case TIOCSWINSZ:
        return set_window_size(p, arg);

    case TIOCGPGRP:
    {
        int32_t group = (int32_t)p->foreground;
        return kerr_to_errno(copy_to_user(arg, &group, sizeof(group)));
    }

    default:
        return -ENOTTY;
    }
}

static int32_t master_read(file_t *file, void *buf, uint32_t count)
{
    pty_t *p = pty_of(file);
    uint8_t chunk[CHUNK];
    uint32_t n;

    uint32_t flags = spin_lock_irqsave(&lock);
    while (p->output.count == 0)
    {
        if (p->slave_was_open && p->slave_opens == 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            return -EIO; // the slave side hung up
        }
        int32_t err = wait_on(p, file);
        if (err != 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            return err;
        }
    }
    n = ring_take(&p->output, chunk, count < CHUNK ? count : CHUNK);
    wait_queue_wake_all(&p->waiters);
    spin_unlock_irqrestore(&lock, flags);

    memcpy(buf, chunk, n);
    poll_notify();
    return (int32_t)n;
}

static int32_t master_write(file_t *file, const void *buf, uint32_t count)
{
    pty_t *p = pty_of(file);
    const uint8_t *src = buf;
    uint32_t done = 0;

    while (done < count)
    {
        uint8_t chunk[CHUNK];
        uint32_t size = count - done < CHUNK ? count - done : CHUNK;
        memcpy(chunk, src + done, size);

        uint32_t used = 0;
        int sig = 0;
        uint32_t group = 0;
        int32_t err = 0;
        uint32_t flags = spin_lock_irqsave(&lock);
        while (used < size)
        {
            if (!input_room(p))
            {
                if (used > 0 || done > 0)
                {
                    break; // return what is written; the next call waits
                }
                err = wait_on(p, file);
                if (err != 0)
                {
                    break;
                }
                continue;
            }
            sig = type_byte(p, chunk[used++]);
            if (sig != 0)
            {
                group = p->foreground;
                break;
            }
        }
        if (used > 0)
        {
            wait_queue_wake_all(&p->waiters);
        }
        spin_unlock_irqrestore(&lock, flags);

        if (used > 0)
        {
            poll_notify();
        }
        if (sig != 0 && group != 0)
        {
            signal_send_group(group, sig);
        }
        done += used;
        if (err != 0)
        {
            return done > 0 ? (int32_t)done : err;
        }
        if (used < size && sig == 0)
        {
            break; // full: a short write
        }
    }
    return (int32_t)done;
}

static int32_t master_ioctl(file_t *file, uint32_t request, void *arg)
{
    pty_t *p = pty_of(file);
    switch (request)
    {
    case TIOCGPTN:
    {
        uint32_t index = p->index;
        return kerr_to_errno(copy_to_user(arg, &index, sizeof(index)));
    }

    case TIOCSPTLCK:
    {
        int32_t value;
        if (kerr_failed(copy_from_user(&value, arg, sizeof(value))))
        {
            return -EFAULT;
        }
        p->locked = value != 0;
        return 0;
    }

    default:
        return common_ioctl(p, request, arg);
    }
}

static uint32_t master_poll(file_t *file)
{
    pty_t *p = pty_of(file);
    uint32_t events = 0;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (p->output.count > 0)
    {
        events |= POLLIN;
    }
    if (p->slave_was_open && p->slave_opens == 0)
    {
        events |= POLLIN | POLLHUP; // a read gives EIO at once
    }
    if (input_room(p))
    {
        events |= POLLOUT;
    }
    spin_unlock_irqrestore(&lock, flags);
    return events;
}

// Linux's device numbers: /dev/ptmx, and the major of /dev/pts/N
#define PTMX_NUMBER DEVICE_NUMBER(5, 2)
#define PTS_MAJOR   136

/** the stat of the master side: /dev/ptmx */
static int32_t master_stat(file_t *file, struct kstat *st)
{
    (void)file;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0620;
    st->st_nlink = 1;
    st->st_rdev = PTMX_NUMBER;
    st->st_blksize = CHUNK;
    return 0;
}

/** the stat of the slave side: /dev/pts/N (ttyname looks for it) */
static int32_t slave_stat(file_t *file, struct kstat *st)
{
    master_stat(file, st);
    st->st_rdev = DEVICE_NUMBER(PTS_MAJOR, pty_of(file)->index);
    return 0;
}

static void master_release(file_t *file)
{
    pty_t *p = pty_of(file);
    uint32_t flags = spin_lock_irqsave(&lock);
    p->master_open = false;
    uint32_t session = p->slave_opens > 0 ? p->session : 0;
    uint32_t group = p->slave_opens > 0 ? p->foreground : 0;
    wait_queue_wake_all(&p->waiters);
    pty_t *unused = unhook_if_unused(p);
    spin_unlock_irqrestore(&lock, flags);

    // the terminal is gone: hang up its session
    if (session != 0)
    {
        signal_send_group(session, SIGHUP);
        signal_send_group(session, SIGCONT);
    }
    if (group != 0 && group != session)
    {
        signal_send_group(group, SIGHUP);
        signal_send_group(group, SIGCONT);
    }
    poll_notify();
    kfree(unused);
    kfree(file);
}

static const file_ops_t master_ops = {
    .read = master_read,
    .write = master_write,
    .ioctl = master_ioctl,
    .stat = master_stat,
    .poll = master_poll,
    .release = master_release,
};

static kerr_t ptmx_open(device_t *dev, uint32_t open_flags, file_t **out)
{
    (void)dev;
    pty_t *p = kmalloc(sizeof(*p));
    file_t *file = file_new(&master_ops, O_RDWR | (open_flags & O_NONBLOCK));
    if (!p || !file)
    {
        kfree(p);
        kfree(file);
        return KERR_NOMEM;
    }
    memset(p, 0, sizeof(*p));
    p->master_open = true;
    p->locked = true;
    default_termios(&p->termios);
    p->size.ws_row = 24;
    p->size.ws_col = 80;
    wait_queue_init(&p->waiters);

    bool added = false;
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = 0; i < PTY_MAX; i++)
    {
        if (!pairs[i])
        {
            p->index = i;
            pairs[i] = p;
            added = true;
            break;
        }
    }
    spin_unlock_irqrestore(&lock, flags);

    if (!added)
    {
        kfree(p);
        kfree(file);
        return KERR_BUSY;
    }
    file->device_data = p;
    *out = file;
    return KERR_OK;
}

/**
 * job control: a process of the terminal's session that is not in the
 * foreground group may not read.
 * @return 0 to go on, or a negative errno.
 */
static int32_t check_background_read(pty_t *p)
{
    process_t *proc = process_current();
    if (!proc || p->session == 0 || proc->sid != p->session || proc->pgid == p->foreground)
    {
        return 0;
    }
    if (signal_refused(proc, SIGTTIN))
    {
        return -EIO;
    }
    signal_send_group(proc->pgid, SIGTTIN);
    return -ERESTARTSYS;
}

static int32_t slave_read(file_t *file, void *buf, uint32_t count)
{
    pty_t *p = pty_of(file);
    int32_t err = check_background_read(p);
    if (err != 0)
    {
        return err;
    }
    if (count == 0)
    {
        return 0;
    }

    uint8_t chunk[CHUNK];
    uint32_t want = count < CHUNK ? count : CHUNK;
    uint32_t n = 0;

    uint32_t flags = spin_lock_irqsave(&lock);
    while (!slave_readable(p))
    {
        if (!canonical(p) && p->termios.c_cc[VMIN] == 0)
        {
            break; // VMIN 0: do not wait
        }
        err = wait_on(p, file);
        if (err != 0)
        {
            spin_unlock_irqrestore(&lock, flags);
            return err;
        }
    }

    if (canonical(p))
    {
        if (p->line_count > 0)
        {
            uint16_t *line = &p->lines[p->line_head];
            n = ring_take(&p->input, chunk, want < *line ? want : *line);
            *line = (uint16_t)(*line - n);
            if (*line == 0)
            {
                // the line is read (or it was an end of file)
                p->line_head = (p->line_head + 1) % MAX_LINES;
                p->line_count--;
            }
        }
    }
    else
    {
        n = ring_take(&p->input, chunk, want);
    }
    wait_queue_wake_all(&p->waiters);
    spin_unlock_irqrestore(&lock, flags);

    memcpy(buf, chunk, n);
    poll_notify();
    return (int32_t)n; // 0: the end of file, or the master is gone
}

static int32_t slave_write(file_t *file, const void *buf, uint32_t count)
{
    pty_t *p = pty_of(file);
    const uint8_t *src = buf;
    uint32_t done = 0;

    while (done < count)
    {
        uint8_t chunk[CHUNK];
        uint32_t size = count - done < CHUNK ? count - done : CHUNK;
        memcpy(chunk, src + done, size);

        uint32_t used = 0;
        int32_t err = 0;
        uint32_t flags = spin_lock_irqsave(&lock);
        while (used < size)
        {
            if (!p->master_open)
            {
                err = -EIO;
                break;
            }
            if (put_output(p, chunk[used]))
            {
                used++;
                continue;
            }
            if (used > 0 || done > 0)
            {
                break; // a short write
            }
            err = wait_on(p, file);
            if (err != 0)
            {
                break;
            }
        }
        if (used > 0)
        {
            wait_queue_wake_all(&p->waiters);
        }
        spin_unlock_irqrestore(&lock, flags);

        if (used > 0)
        {
            poll_notify();
        }
        done += used;
        if (err != 0)
        {
            return done > 0 ? (int32_t)done : err;
        }
        if (used < size)
        {
            break;
        }
    }
    return (int32_t)done;
}

/**
 * TIOCSPGRP: make a process group of the terminal's session the
 * foreground one.
 */
static int32_t set_foreground(pty_t *p, const void *arg)
{
    process_t *proc = process_current();
    int32_t group;
    if (kerr_failed(copy_from_user(&group, arg, sizeof(group))))
    {
        return -EFAULT;
    }
    if (p->session == 0 || proc->sid != p->session)
    {
        return -ENOTTY;
    }
    if (group <= 0 || !process_group_exists((uint32_t)group, proc->sid))
    {
        return -EPERM;
    }
    if (proc->pgid != p->foreground && !signal_refused(proc, SIGTTOU))
    {
        signal_send_group(proc->pgid, SIGTTOU);
        return -ERESTARTSYS;
    }
    p->foreground = (uint32_t)group;
    return 0;
}

static int32_t slave_ioctl(file_t *file, uint32_t request, void *arg)
{
    pty_t *p = pty_of(file);
    process_t *proc = process_current();

    switch (request)
    {
    case TIOCSPGRP:
        return set_foreground(p, arg);

    case TIOCSCTTY:
        // a session leader takes the terminal if no session has it, or,
        // with argument 1, root takes it from the session that has it
        if (!proc || proc->sid != proc->pid)
        {
            return -EPERM;
        }
        if (p->session != 0 && p->session != proc->sid &&
            !((uintptr_t)arg == 1 && proc->euid == 0))
        {
            return -EPERM;
        }
        p->session = proc->sid;
        p->foreground = proc->pgid;
        return 0;

    default:
        return common_ioctl(p, request, arg);
    }
}

static uint32_t slave_poll(file_t *file)
{
    pty_t *p = pty_of(file);
    uint32_t events = 0;
    uint32_t flags = spin_lock_irqsave(&lock);
    if (slave_readable(p))
    {
        events |= POLLIN;
    }
    if (!p->master_open)
    {
        events |= POLLHUP;
    }
    else if (ring_room(&p->output) >= 2)
    {
        events |= POLLOUT;
    }
    spin_unlock_irqrestore(&lock, flags);
    return events;
}

static void slave_release(file_t *file)
{
    pty_t *p = pty_of(file);
    uint32_t flags = spin_lock_irqsave(&lock);
    p->slave_opens--;
    if (p->slave_opens == 0)
    {
        p->session = 0; // no longer anyone's terminal
        p->foreground = 0;
    }
    wait_queue_wake_all(&p->waiters);
    pty_t *unused = unhook_if_unused(p);
    spin_unlock_irqrestore(&lock, flags);
    poll_notify();
    kfree(unused);
    kfree(file);
}

static const file_ops_t slave_ops = {
    .read = slave_read,
    .write = slave_write,
    .ioctl = slave_ioctl,
    .stat = slave_stat,
    .poll = slave_poll,
    .release = slave_release,
};

static kerr_t slave_open(device_t *dev, uint32_t open_flags, file_t **out)
{
    uint32_t index = (uint32_t)(uintptr_t)dev->driver_data;
    file_t *file = file_new(&slave_ops, (open_flags & O_ACCMODE) | (open_flags & O_NONBLOCK));
    if (!file)
    {
        return KERR_NOMEM;
    }

    kerr_t result = KERR_OK;
    uint32_t flags = spin_lock_irqsave(&lock);
    pty_t *p = pairs[index];
    if (!p)
    {
        result = KERR_NOENT;
    }
    else if (p->locked || !p->master_open)
    {
        result = KERR_IO;
    }
    else
    {
        p->slave_opens++;
        p->slave_was_open = true;
        file->device_data = p;
    }
    spin_unlock_irqrestore(&lock, flags);

    if (kerr_failed(result))
    {
        kfree(file);
        return result;
    }
    *out = file;
    return KERR_OK;
}

static const device_ops_t ptmx_ops = {
    .open = ptmx_open,
};

static const device_ops_t slave_device_ops = {
    .open = slave_open,
};

void pty_init(void)
{
    spinlock_init(&lock);
    for (uint32_t i = 0; i < PTY_MAX; i++)
    {
        // the name is the number (PTY_MAX is below 100)
        device_t *dev = &slave_devices[i];
        char *name = dev->name;
        if (i >= 10)
        {
            *name++ = (char)('0' + i / 10);
        }
        *name++ = (char)('0' + i % 10);
        *name = '\0';
        dev->type = DEVICE_TYPE_CHAR;
        dev->ops = &slave_device_ops;
        dev->driver_data = (void *)(uintptr_t)i;
        dev->number = DEVICE_NUMBER(PTS_MAJOR, i);
    }

    strncpy(ptmx_device.name, "ptmx", DEVICE_NAME_MAX - 1);
    ptmx_device.type = DEVICE_TYPE_CHAR;
    ptmx_device.ops = &ptmx_ops;
    ptmx_device.number = PTMX_NUMBER;
    device_register(&ptmx_device);
}

device_t *pty_slave_device(uint32_t index)
{
    if (index >= PTY_MAX)
    {
        return NULL;
    }
    uint32_t flags = spin_lock_irqsave(&lock);
    bool exists = pairs[index] != NULL;
    spin_unlock_irqrestore(&lock, flags);
    return exists ? &slave_devices[index] : NULL;
}

bool pty_next(uint32_t *index)
{
    bool found = false;
    uint32_t flags = spin_lock_irqsave(&lock);
    for (uint32_t i = *index; i < PTY_MAX; i++)
    {
        if (pairs[i])
        {
            *index = i;
            found = true;
            break;
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    return found;
}
