/**
 * the console as an open-file description (kernel/vfs/file.h) and as a
 * terminal: reads come from the keyboard through a line discipline set by
 * termios, writes go to the screen. there is one description, shared by
 * every process's stdio; it is never released.
 *
 * the terminal belongs to one session (init's) and has a foreground
 * process group. with ISIG, Ctrl+C, Ctrl+\ and Ctrl+Z send SIGINT, SIGQUIT
 * and SIGTSTP to that group; a process of a background group that reads
 * gets SIGTTIN.
 *
 * without ICANON, the special keys (arrows, Home, End, Insert, Delete,
 * Page Up and Page Down) come as the escape sequences a VT terminal
 * sends, so a program reads the console as it reads a pseudo-terminal.
 * with ICANON they are ignored.
 *
 * locking: the settings change only through ioctls in process context
 * and are read by the keyboard interrupt (the signal keys); a torn read
 * there could only miss one key press.
 */

#include "console.h"
#include "termios.h"

#include "common/memory.h"
#include "common/stddef.h"
#include "drivers/keyboard/keyboard.h"
#include "kernel/errno.h"
#include "kernel/process/process.h"
#include "kernel/usermode/uaccess.h"
#include "kernel/vfs/file.h"
#include "kernel/vfs/poll.h"

static struct ktermios settings;
static uint32_t terminal_session;  // the session it controls (0: none)
static volatile uint32_t foreground; // the foreground process group

#define CTRL(key) ((key) & 0x1f)

/**
 * the signal a key sends with ISIG, or 0.
 */
static int signal_for_key(char c)
{
    if (c == (char)settings.c_cc[VINTR])
        return SIGINT;
    if (c == (char)settings.c_cc[VQUIT])
        return SIGQUIT;
    if (c == (char)settings.c_cc[VSUSP])
        return SIGTSTP;
    return 0;
}

/**
 * keyboard filter: the signal keys (keyboard interrupt).
 */
static bool filter_key(const keyboard_event_t *event)
{
    if (!(settings.c_lflag & ISIG) || event->type != KEY_EVENT_CHAR || event->character == 0)
    {
        return false;
    }

    int sig = signal_for_key(event->character);
    if (sig == 0)
    {
        return false;
    }

    if (settings.c_lflag & ECHO)
    {
        console_putchar('^');
        console_putchar((char)(event->character + '@'));
        console_putchar('\r');
        console_putchar('\n');
    }
    signal_send_group(foreground, sig);
    return true;
}

void console_terminal_init(uint32_t init_pid)
{
    memset(&settings, 0, sizeof(settings));
    settings.c_iflag = ICRNL;
    settings.c_oflag = OPOST | ONLCR;
    settings.c_cflag = CS8 | CREAD;
    settings.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    settings.c_cc[VINTR] = CTRL('c');
    settings.c_cc[VQUIT] = CTRL('\\');
    settings.c_cc[VERASE] = '\b';
    settings.c_cc[VKILL] = CTRL('u');
    settings.c_cc[VEOF] = CTRL('d');
    settings.c_cc[VSUSP] = CTRL('z');
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;

    terminal_session = init_pid;
    foreground = init_pid;
    keyboard_set_filter(filter_key);
}

/**
 * true if a signal is ignored or blocked by the process.
 */
static bool signal_refused(const process_t *proc, int sig)
{
    return proc->signals.actions[sig].handler == SIG_IGN ||
           (proc->signals.blocked & SIG_BIT(sig));
}

/**
 * job control for a read: a process of the terminal's session that is
 * not in the foreground group may not read.
 * @return 0 to go on, or a negative errno.
 */
static int32_t check_background_read(process_t *proc)
{
    if (!proc || proc->sid != terminal_session || proc->pgid == foreground)
    {
        return 0;
    }
    if (signal_refused(proc, SIGTTIN))
    {
        return -EIO;
    }
    // by default this stops the group until the shell brings it back
    signal_send_group(proc->pgid, SIGTTIN);
    return -ERESTARTSYS;
}

static void echo(char c)
{
    if (settings.c_lflag & ECHO)
    {
        console_putchar(c);
    }
}

// the rest of an escape sequence for a special key, not read yet
static char pending[8];
static uint32_t pending_head;
static uint32_t pending_count;

/**
 * the escape sequence a VT terminal sends for a special key, or NULL.
 */
static const char *sequence_for(keyboard_special_key_t key)
{
    switch (key)
    {
    case KEY_ARROW_UP:
        return "\x1b[A";
    case KEY_ARROW_DOWN:
        return "\x1b[B";
    case KEY_ARROW_RIGHT:
        return "\x1b[C";
    case KEY_ARROW_LEFT:
        return "\x1b[D";
    case KEY_HOME:
        return "\x1b[H";
    case KEY_END:
        return "\x1b[F";
    case KEY_INSERT:
        return "\x1b[2~";
    case KEY_DELETE:
        return "\x1b[3~";
    case KEY_PAGE_UP:
        return "\x1b[5~";
    case KEY_PAGE_DOWN:
        return "\x1b[6~";
    default:
        return NULL;
    }
}

/**
 * true if a read would get a byte without waiting.
 */
static bool input_available(void)
{
    return pending_count > 0 || keyboard_available();
}

/**
 * the next typed byte. with sequences, a special key gives the first byte
 * of its escape sequence and the next calls give the rest; without, the
 * special keys are skipped.
 * @return the byte, or -1 if a signal ended the wait.
 */
static int next_byte(bool sequences)
{
    if (pending_count > 0)
    {
        pending_count--;
        return (unsigned char)pending[pending_head++];
    }
    for (;;)
    {
        keyboard_event_t event;
        if (!keyboard_wait_event(&event))
        {
            return -1;
        }
        if (event.type == KEY_EVENT_CHAR)
        {
            return (unsigned char)event.character;
        }
        const char *seq = event.type == KEY_EVENT_SPECIAL && sequences
                              ? sequence_for(event.special)
                              : NULL;
        if (seq)
        {
            pending_head = 0;
            pending_count = 0;
            for (uint32_t i = 1; seq[i] != '\0'; i++)
            {
                pending[pending_count++] = seq[i];
            }
            return (unsigned char)seq[0];
        }
    }
}

/**
 * the next typed character (with ICRNL and INLCR), or -1 if a signal
 * ended the wait.
 * @param sequences true to get special keys as escape sequences.
 */
static int next_char(bool sequences)
{
    for (;;)
    {
        int key = next_byte(sequences);
        if (key < 0)
        {
            return -1;
        }
        if (key == '\r')
        {
            if (settings.c_iflag & IGNCR)
                continue;
            if (settings.c_iflag & ICRNL)
                key = '\n';
        }
        else if (key == '\n' && (settings.c_iflag & INLCR))
        {
            key = '\r';
        }
        return key;
    }
}

/**
 * ICANON: a line with editing (erase, kill), up to count bytes. the
 * newline ends it and is stored; VEOF at the start of a line is end of
 * file (0 bytes).
 */
static int32_t read_line(char *buf, uint32_t count)
{
    uint32_t i = 0;
    while (i < count)
    {
        int key = next_char(false);
        if (key < 0)
        {
            // a signal: the bytes so far are the result, if there are any
            return i > 0 ? (int32_t)i : -ERESTARTSYS;
        }
        char c = (char)key;

        if (c == '\n')
        {
            echo('\r');
            echo('\n');
            buf[i++] = '\n';
            break;
        }
        if (c == (char)settings.c_cc[VEOF])
        {
            break; // end of file at the start of a line, else end of line
        }
        if (c == (char)settings.c_cc[VERASE] || c == 0x7f)
        {
            if (i > 0)
            {
                i--;
                echo('\b');
                if (settings.c_lflag & ECHOE)
                {
                    echo(' ');
                    echo('\b');
                }
            }
            continue;
        }
        if (c == (char)settings.c_cc[VKILL])
        {
            while (i > 0)
            {
                i--;
                echo('\b');
                echo(' ');
                echo('\b');
            }
            continue;
        }

        buf[i++] = c;
        echo(c);
    }
    return (int32_t)i;
}

/**
 * no ICANON: bytes as they are typed. with VMIN > 0, wait for the first
 * one, then take what else is typed already; with VMIN 0, do not wait.
 */
static int32_t read_raw(char *buf, uint32_t count)
{
    uint32_t i = 0;
    while (i < count)
    {
        if (i > 0 || settings.c_cc[VMIN] == 0)
        {
            if (!input_available())
            {
                break;
            }
        }
        int key = next_char(true);
        if (key < 0)
        {
            return i > 0 ? (int32_t)i : -ERESTARTSYS;
        }
        buf[i++] = (char)key;
        echo((char)key);
    }
    return (int32_t)i;
}

/**
 * XKEYEVENTS: one keyboard_event_t per read. with VMIN 0 the read does not
 * wait: it returns 0 if no event is ready.
 */
static int32_t read_event(char *buf, uint32_t count)
{
    if (count < sizeof(keyboard_event_t))
    {
        return -EINVAL;
    }
    if (settings.c_cc[VMIN] == 0 && !keyboard_available())
    {
        return 0;
    }

    keyboard_event_t event;
    if (!keyboard_wait_event(&event))
    {
        return -ERESTARTSYS;
    }
    memcpy(buf, &event, sizeof(event));
    return (int32_t)sizeof(event);
}

static int32_t console_file_read(file_t *file, void *buf, uint32_t count)
{
    (void)file;
    int32_t err = check_background_read(process_current());
    if (err < 0)
    {
        return err;
    }

    if (settings.c_lflag & XKEYEVENTS)
    {
        return read_event(buf, count);
    }
    if (settings.c_lflag & ICANON)
    {
        return read_line(buf, count);
    }
    return read_raw(buf, count);
}

static int32_t console_file_write(file_t *file, const void *buf, uint32_t count)
{
    (void)file;
    // one batched console update per write: the caret is placed and the
    // display flushed once at the end, not after every byte
    console_write_bytes(buf, count);
    return (int32_t)count;
}

/**
 * TIOCSPGRP: make a process group of the terminal's session the
 * foreground one.
 */
static int32_t set_foreground(const void *arg)
{
    process_t *proc = process_current();
    int32_t pgrp;
    if (kerr_failed(copy_from_user(&pgrp, arg, sizeof(pgrp))))
    {
        return -EFAULT;
    }
    if (proc->sid != terminal_session)
    {
        return -ENOTTY;
    }
    if (pgrp <= 0 || !process_group_exists((uint32_t)pgrp, proc->sid))
    {
        return -EPERM;
    }

    // a background process may only do this if it accepts no SIGTTOU
    if (proc->pgid != foreground && !signal_refused(proc, SIGTTOU))
    {
        signal_send_group(proc->pgid, SIGTTOU);
        return -ERESTARTSYS;
    }

    foreground = (uint32_t)pgrp;
    return 0;
}

static int32_t console_file_ioctl(file_t *file, uint32_t request, void *arg)
{
    (void)file;
    process_t *proc = process_current();

    switch (request)
    {
    case TCGETS:
        return kerr_to_errno(copy_to_user(arg, &settings, sizeof(settings)));

    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    {
        struct ktermios next;
        if (kerr_failed(copy_from_user(&next, arg, sizeof(next))))
        {
            return -EFAULT;
        }
        if (request == TCSETSF)
        {
            keyboard_flush();
            pending_count = 0;
        }
        settings = next; // output is written at once, so there is nothing to drain
        keyboard_set_raw_events((settings.c_lflag & (XKEYEVENTS | XKEYRAW)) ==
                                (XKEYEVENTS | XKEYRAW));
        return 0;
    }

    case TIOCGPGRP:
    {
        int32_t pgrp = (int32_t)foreground;
        return kerr_to_errno(copy_to_user(arg, &pgrp, sizeof(pgrp)));
    }

    case TIOCSPGRP:
        return set_foreground(arg);

    case TIOCSCTTY:
        // a session leader takes the terminal if no session has it, or,
        // with argument 1, root takes it from the session that has it
        if (!proc || proc->sid != proc->pid)
        {
            return -EPERM;
        }
        if (terminal_session != 0 && terminal_session != proc->sid &&
            !((uintptr_t)arg == 1 && proc->euid == 0))
        {
            return -EPERM;
        }
        terminal_session = proc->sid;
        foreground = proc->pgid;
        return 0;

    case TIOCGWINSZ:
    {
        uint32_t rows;
        uint32_t cols;
        console_size(&rows, &cols);
        struct kwinsize size = {(uint16_t)rows, (uint16_t)cols, 0, 0};
        return kerr_to_errno(copy_to_user(arg, &size, sizeof(size)));
    }

    default:
        return -ENOTTY;
    }
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

static uint32_t console_file_poll(file_t *file)
{
    (void)file;
    return POLLOUT | (input_available() ? POLLIN : 0);
}

static const file_ops_t console_file_ops = {
    .read = console_file_read,
    .write = console_file_write,
    .ioctl = console_file_ioctl,
    .stat = console_file_stat,
    .poll = console_file_poll,
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
