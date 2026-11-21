/**
 * @file ptytest.c
 * @brief test pseudo-terminals (/dev/ptmx, /dev/pts/N).
 *
 * usage: ptytest
 *
 * checks, in order:
 *   open    the slave cannot be opened before unlockpt; ptsname; the pair
 *           shows up in /dev/pts
 *   canon   a line goes to the slave when it ends, with erase, kill and
 *           end of file; the master reads the echo; ONLCR on output
 *   raw     without ICANON, ISIG and ECHO, bytes go through as they are
 *   signals a child with the slave as its terminal gets SIGWINCH when
 *           the master sets the size, and SIGINT for Ctrl+C
 *   hangup  closing the master sends SIGHUP to the session, and the slave
 *           reads the end of file; closing the slave gives the master EIO
 *   shell   /bin/sh on the slave: a command runs, the line editor takes
 *           the escape sequences for the arrow keys and Delete, history
 *           works, Ctrl+C drops a line, and Ctrl+D ends the shell
 *   free    the checks run in a child 4 times, and free memory is the same
 *           after runs 2, 3 and 4
 *
 * prints "ptytest: ok" when all pass.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int failed(const char *what)
{
    printf("ptytest: %s failed (errno %d)\n", what, errno);
    return 1;
}

/**
 * free memory in kB, from /proc/meminfo.
 */
static long free_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;
    char line[128];
    long value = -1;
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "MemFree:", 8) == 0)
        {
            value = atol(line + 8);
            break;
        }
    }
    fclose(f);
    return value;
}

/**
 * read from fd until want bytes are in buf, or a second passes.
 * @return the bytes read.
 */
static int read_some(int fd, char *buf, int want)
{
    int got = 0;
    while (got < want)
    {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 1000) != 1)
            break;
        int n = (int)read(fd, buf + got, want - got);
        if (n <= 0)
            break;
        got += n;
    }
    buf[got] = '\0';
    return got;
}

/**
 * true if fd has something to read now.
 */
static bool readable(int fd)
{
    struct pollfd pfd = {fd, POLLIN, 0};
    return poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN);
}

static bool listed(int index)
{
    char name[8];
    snprintf(name, sizeof(name), "%d", index);
    DIR *dir = opendir("/dev/pts");
    if (!dir)
        return false;
    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        if (strcmp(entry->d_name, name) == 0)
            found = true;
    }
    closedir(dir);
    return found;
}

static int check_open(void)
{
    int m = posix_openpt(O_RDWR);
    if (m < 0)
        return failed("open: posix_openpt");
    char *name = ptsname(m);
    if (!name || strncmp(name, "/dev/pts/", 9) != 0)
        return failed("open: ptsname");
    int index = atoi(name + 9);
    errno = 0;
    if (open(name, O_RDWR) >= 0 || errno != EIO)
        return failed("open: the slave is locked before unlockpt");
    if (unlockpt(m) != 0)
        return failed("open: unlockpt");
    int s = open(name, O_RDWR);
    if (s < 0 || !isatty(s))
        return failed("open: the slave");
    if (!listed(index))
        return failed("open: listed in /dev/pts");
    close(s);
    close(m);
    if (listed(index))
        return failed("open: gone from /dev/pts after close");
    return 0;
}

static int check_canon(void)
{
    int m, s;
    char buf[64];
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("canon: openpty");

    // a line is held back until it ends
    write(m, "hel", 3);
    if (readable(s))
        return failed("canon: half a line is not readable");
    write(m, "lo\r", 3);
    if (read(s, buf, sizeof(buf)) != 6 || memcmp(buf, "hello\n", 6) != 0)
        return failed("canon: the line, CR as NL");
    if (read_some(m, buf, 7) != 7 || strcmp(buf, "hello\r\n") != 0)
        return failed("canon: the echo");

    // erase and kill
    write(m, "ab\x7f" "c\n", 5);
    if (read(s, buf, sizeof(buf)) != 3 || memcmp(buf, "ac\n", 3) != 0)
        return failed("canon: erase");
    write(m, "xyz\x15ok\n", 7);
    if (read(s, buf, sizeof(buf)) != 3 || memcmp(buf, "ok\n", 3) != 0)
        return failed("canon: kill");

    // a short read takes the rest of the line next time
    write(m, "abcdef\n", 7);
    if (read(s, buf, 4) != 4 || read(s, buf, sizeof(buf)) != 3 || memcmp(buf, "ef\n", 3) != 0)
        return failed("canon: a line in two reads");

    // VEOF: the line without it, then an end of file
    write(m, "end\x04\x04", 5);
    if (read(s, buf, sizeof(buf)) != 3 || memcmp(buf, "end", 3) != 0 ||
        read(s, buf, sizeof(buf)) != 0)
        return failed("canon: end of file");

    // output: NL becomes CR NL
    while (readable(m))
        read(m, buf, sizeof(buf)); // the echo so far
    write(s, "a\nb", 3);
    if (read_some(m, buf, 4) != 4 || strcmp(buf, "a\r\nb") != 0)
        return failed("canon: ONLCR");

    close(s);
    close(m);
    return 0;
}

static int check_raw(void)
{
    int m, s;
    char buf[64];
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("raw: openpty");
    write(m, "pend", 4); // typed before the switch: readable after it

    struct termios t;
    if (tcgetattr(s, &t) != 0)
        return failed("raw: tcgetattr");
    cfmakeraw(&t);
    if (tcsetattr(s, TCSANOW, &t) != 0)
        return failed("raw: tcsetattr");
    if (read(s, buf, sizeof(buf)) != 4 || memcmp(buf, "pend", 4) != 0)
        return failed("raw: bytes typed before the switch");
    while (readable(m))
        read(m, buf, sizeof(buf)); // their echo

    write(m, "\x03q\r", 3);
    if (read_some(s, buf, 3) != 3 || memcmp(buf, "\x03q\r", 3) != 0)
        return failed("raw: bytes as they are");
    if (readable(m))
        return failed("raw: no echo");
    write(s, "\n", 1);
    if (read_some(m, buf, 1) != 1 || buf[0] != '\n')
        return failed("raw: no output processing");

    // VMIN 0: a read does not wait
    t.c_cc[VMIN] = 0;
    tcsetattr(s, TCSANOW, &t);
    if (read(s, buf, sizeof(buf)) != 0)
        return failed("raw: VMIN 0");
    close(s);
    close(m);
    return 0;
}

static volatile sig_atomic_t got_winch;
static volatile sig_atomic_t got_int;

static void on_winch(int sig)
{
    (void)sig;
    got_winch = 1;
}

static void on_int(int sig)
{
    (void)sig;
    got_int = 1;
}

/**
 * the child for the signal check: its terminal is the slave.
 */
static int signal_child(int s)
{
    signal(SIGWINCH, on_winch);
    signal(SIGINT, on_int);
    if (login_tty(s) != 0)
        return 1;
    write(1, "ready\n", 6);
    for (int i = 0; i < 200 && !got_winch; i++)
        usleep(10000);
    struct winsize size;
    if (!got_winch || ioctl(0, TIOCGWINSZ, &size) != 0 || size.ws_row != 30 ||
        size.ws_col != 100)
        return 2;
    write(1, "winch\n", 6);
    for (int i = 0; i < 200 && !got_int; i++)
        usleep(10000);
    return got_int ? 0 : 3;
}

static int check_signals(void)
{
    int m, s;
    char buf[64];
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("signals: openpty");
    pid_t pid = fork();
    if (pid == 0)
    {
        close(m);
        _exit(signal_child(s));
    }
    close(s);

    if (read_some(m, buf, 7) != 7 || strcmp(buf, "ready\r\n") != 0)
        return failed("signals: the child is ready");
    struct winsize size = {30, 100, 0, 0};
    if (ioctl(m, TIOCSWINSZ, &size) != 0)
        return failed("signals: TIOCSWINSZ");
    if (read_some(m, buf, 7) != 7 || strcmp(buf, "winch\r\n") != 0)
        return failed("signals: SIGWINCH");
    write(m, "\x03", 1);
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return failed("signals: SIGINT");
    if (read_some(m, buf, 4) != 4 || strcmp(buf, "^C\r\n") != 0)
        return failed("signals: the echo of Ctrl+C");
    close(m);
    return 0;
}

static int check_hangup(void)
{
    int m, s;
    char buf[16];
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("hangup: openpty");
    pid_t pid = fork();
    if (pid == 0)
    {
        close(m);
        if (login_tty(dup(s)) != 0)
            _exit(1);
        for (;;)
            usleep(10000); // until SIGHUP
    }
    usleep(100000);
    close(m);
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFSIGNALED(status) || WTERMSIG(status) != SIGHUP)
        return failed("hangup: SIGHUP");
    if (read(s, buf, sizeof(buf)) != 0)
        return failed("hangup: the slave reads the end of file");
    errno = 0;
    if (write(s, "x", 1) != -1 || errno != EIO)
        return failed("hangup: the slave's writes give EIO");
    close(s);

    // the other way: the master reads EIO after the slave closes
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("hangup: openpty");
    close(s);
    struct pollfd pfd = {m, POLLIN, 0};
    if (poll(&pfd, 1, 0) != 1 || !(pfd.revents & POLLHUP))
        return failed("hangup: POLLHUP on the master");
    errno = 0;
    if (read(m, buf, sizeof(buf)) != -1 || errno != EIO)
        return failed("hangup: the master reads EIO");
    close(m);
    return 0;
}

/**
 * read the master until text appears in what was read, or a while
 * passes. what was read is kept in out (up to size bytes).
 * @return true if the text appeared.
 */
static bool wait_for(int m, const char *text, char *out, int size)
{
    int got = 0;
    out[0] = '\0';
    for (int tries = 0; tries < 40 && !strstr(out, text); tries++)
    {
        struct pollfd pfd = {m, POLLIN, 0};
        if (poll(&pfd, 1, 100) != 1)
            continue;
        int n = (int)read(m, out + got, size - 1 - got);
        if (n <= 0)
            break;
        got += n;
        out[got] = '\0';
        if (got >= size - 1)
            got = 0; // keep the tail only
    }
    return strstr(out, text) != NULL;
}

/**
 * write a string to fd.
 */
static void put(int fd, const char *text)
{
    write(fd, text, strlen(text));
}

/**
 * type a line into the shell and wait until its output has text and the
 * next prompt has come after it. the shell edits a line only while its
 * prompt is up: typed earlier, the bytes would meet the line discipline
 * of the command that is running.
 */
static bool step(int m, const char *input, const char *text)
{
    char out[1024];
    put(m, input);
    if (!wait_for(m, text, out, sizeof(out)))
        return false;
    if (strstr(strstr(out, text), "$ "))
        return true;
    return wait_for(m, "$ ", out, sizeof(out));
}

/**
 * wait for a child, at most a few seconds; kill it after that.
 * @return its exit status, or -1.
 */
static int wait_exit(pid_t pid)
{
    int status;
    for (int i = 0; i < 300; i++)
    {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        usleep(10000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -1;
}

static int check_shell(void)
{
    int m, s;
    char out[1024];
    if (openpty(&m, &s, NULL, NULL, NULL) != 0)
        return failed("shell: openpty");
    pid_t pid = fork();
    if (pid == 0)
    {
        close(m);
        if (login_tty(s) != 0)
            _exit(1);
        char *argv[] = {"/bin/sh", NULL};
        char *envp[] = {NULL};
        execve("/bin/sh", argv, envp);
        _exit(2);
    }
    close(s);

    if (!wait_for(m, "$ ", out, sizeof(out)))
        return failed("shell: the prompt");

    // a command; Enter is CR on a terminal
    if (!step(m, "echo one\r", "one\r\n"))
        return failed("shell: a command");

    // Left twice, then a letter: it goes in before the last two
    if (!step(m, "echo abcd\x1b[D\x1b[Dx\r", "abxcd\r\n"))
        return failed("shell: the arrow keys");

    // Home, Delete (removes the 'z'), and the SS3 form of End
    if (!step(m, "zecho del\x1b[H\x1b[3~\x1bOFok\r", "delok\r\n"))
        return failed("shell: Home, Delete and End");

    // Up gives the last line again
    if (!step(m, "\x1b[A\r", "delok\r\n"))
        return failed("shell: history");

    // Ctrl+C drops the line; Backspace (DEL) deletes
    if (!step(m, "echo gone\x03", "^C"))
        return failed("shell: Ctrl+C");
    if (!step(m, "echo kepz\x7ft\r", "kept\r\n"))
        return failed("shell: Backspace");

    // Ctrl+D on an empty line ends the shell
    put(m, "\x04");
    if (wait_exit(pid) != 0)
        return failed("shell: Ctrl+D");
    close(m);
    return 0;
}

static int run_checks(void)
{
    if (check_open() || check_canon() || check_raw() || check_signals() || check_hangup() ||
        check_shell())
        return 1;
    return 0;
}

int main(void)
{
    long free_after[4];
    for (int run = 0; run < 4; run++)
    {
        pid_t pid = fork();
        if (pid < 0)
            return failed("fork");
        if (pid == 0)
            _exit(run_checks());
        int status;
        if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return 1; // the child printed what failed
        free_after[run] = free_kb();
    }
    if (free_after[1] < 0 || free_after[2] != free_after[1] || free_after[3] != free_after[1])
    {
        printf("ptytest: free memory after each run: %ld %ld %ld %ld kB\n", free_after[0],
               free_after[1], free_after[2], free_after[3]);
        return failed("freeing the pairs");
    }

    printf("ptytest: ok\n");
    return 0;
}
