/**
 * @file sigtest.c
 * @brief Exercise the kernel's signals.
 *
 * usage: sigtest <mode>
 *   handler a handler runs for raise(), with its argument, and the code it
 *           interrupted goes on with its registers and locals intact
 *   mask    a blocked signal waits (sigpending) until it is unblocked
 *   kill    SIGTERM ends a busy child, SIGKILL one asleep in a pipe read
 *   eintr   alarm interrupts a pipe read: EINTR without SA_RESTART, and
 *           the read goes on with SA_RESTART
 *   stop    SIGSTOP, SIGCONT and SIGKILL, seen with WUNTRACED/WCONTINUED
 *   fault   SIGSEGV with a handler; division by zero and an invalid
 *           instruction kill only the process
 *   pipe    SIGPIPE ends a writer; ignored, the write fails with EPIPE
 *   chld    SIGCHLD reaches the parent when a child exits
 *
 * each mode prints "sigtest <mode>: ok" when it passes.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *mode_name;
static volatile sig_atomic_t caught;
static volatile sig_atomic_t caught_signal;

static int failed(const char *what)
{
    printf("sigtest %s: %s (errno %d)\n", mode_name, what, errno);
    return 1;
}

static int ok(void)
{
    printf("sigtest %s: ok\n", mode_name);
    return 0;
}

static void on_signal(int sig)
{
    caught++;
    caught_signal = sig;
}

/**
 * set a handler with sigaction.
 */
static void handle(int sig, void (*handler)(int), int flags)
{
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = flags;
    sigemptyset(&act.sa_mask);
    sigaction(sig, &act, NULL);
}

static int test_handler(void)
{
    handle(SIGUSR1, on_signal, 0);

    // values in registers and on the stack around the signal
    volatile int a = 11;
    volatile int b = 22;
    raise(SIGUSR1);
    if (caught != 1 || caught_signal != SIGUSR1 || a != 11 || b != 22)
        return failed("handler for raise");

    // SA_RESETHAND: the second signal gets the default action (here,
    // ignore: SIGCHLD's default)
    handle(SIGCHLD, on_signal, SA_RESETHAND);
    raise(SIGCHLD);
    raise(SIGCHLD);
    if (caught != 2)
        return failed("SA_RESETHAND");

    struct sigaction old;
    sigaction(SIGUSR1, NULL, &old);
    if (old.sa_handler != on_signal)
        return failed("reading back the handler");
    if (sigaction(SIGKILL, &old, NULL) != -1 || errno != EINVAL)
        return failed("a handler for SIGKILL");
    return ok();
}

static int test_mask(void)
{
    handle(SIGUSR2, on_signal, 0);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigprocmask(SIG_BLOCK, &set, NULL);

    raise(SIGUSR2);
    sigset_t pending;
    sigpending(&pending);
    if (caught != 0 || !sigismember(&pending, SIGUSR2))
        return failed("a blocked signal ran or is not pending");

    sigprocmask(SIG_UNBLOCK, &set, NULL);
    if (caught != 1)
        return failed("the signal did not run when unblocked");
    return ok();
}

static int test_kill(void)
{
    pid_t busy = fork();
    if (busy == 0)
    {
        for (;;)
            ;
    }

    int fds[2];
    pipe(fds);
    pid_t sleeper = fork();
    if (sleeper == 0)
    {
        char byte;
        read(fds[0], &byte, 1); // sleeps: nobody writes
        _exit(0);
    }

    for (int i = 0; i < 10; i++)
        sched_yield();

    int status;
    kill(busy, SIGTERM);
    if (waitpid(busy, &status, 0) != busy || !WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
        return failed("SIGTERM to a busy child");
    kill(sleeper, SIGKILL);
    if (waitpid(sleeper, &status, 0) != sleeper || WTERMSIG(status) != SIGKILL)
        return failed("SIGKILL to a sleeping child");
    if (kill(99999, SIGTERM) != -1 || errno != ESRCH)
        return failed("kill of a missing process");
    return ok();
}

static int test_eintr(void)
{
    int fds[2];
    pipe(fds);
    char byte;

    // no SA_RESTART: the read ends with EINTR
    handle(SIGALRM, on_signal, 0);
    alarm(1);
    if (read(fds[0], &byte, 1) != -1 || errno != EINTR || caught != 1)
        return failed("read without SA_RESTART");

    // SA_RESTART: the read goes on after the handler, until a child
    // writes (after its own alarm)
    handle(SIGALRM, on_signal, SA_RESTART);
    pid_t writer = fork();
    if (writer == 0)
    {
        handle(SIGALRM, on_signal, 0);
        alarm(2);
        pause();
        write(fds[1], "x", 1);
        _exit(0);
    }
    alarm(1);
    if (read(fds[0], &byte, 1) != 1 || byte != 'x' || caught != 2)
        return failed("read with SA_RESTART");
    waitpid(writer, NULL, 0);
    return ok();
}

static int test_stop(void)
{
    pid_t child = fork();
    if (child == 0)
    {
        for (;;)
            ;
    }

    int status;
    kill(child, SIGSTOP);
    if (waitpid(child, &status, WUNTRACED) != child || !WIFSTOPPED(status) ||
        WSTOPSIG(status) != SIGSTOP)
        return failed("WUNTRACED after SIGSTOP");

    kill(child, SIGCONT);
    if (waitpid(child, &status, WCONTINUED) != child || !WIFCONTINUED(status))
        return failed("WCONTINUED after SIGCONT");

    // stopped again, SIGKILL still ends it
    kill(child, SIGSTOP);
    waitpid(child, &status, WUNTRACED);
    kill(child, SIGKILL);
    if (waitpid(child, &status, 0) != child || WTERMSIG(status) != SIGKILL)
        return failed("SIGKILL to a stopped child");
    return ok();
}

static void segv_handler(int sig)
{
    _exit(sig == SIGSEGV ? 42 : 1);
}

/**
 * run fn in a child and return its wait status.
 */
static int child_status(void (*fn)(void))
{
    pid_t child = fork();
    if (child == 0)
    {
        fn();
        _exit(0);
    }
    int status;
    waitpid(child, &status, 0);
    return status;
}

static void read_null(void)
{
    handle(SIGSEGV, segv_handler, 0);
    volatile int *null = NULL;
    (void)*null;
}

static void divide_by_zero(void)
{
    volatile int zero = 0;
    volatile int value = 10 / zero;
    (void)value;
}

static void invalid_instruction(void)
{
    __asm__ volatile("ud2");
}

static int test_fault(void)
{
    int status = child_status(read_null);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        return failed("SIGSEGV handler");
    status = child_status(divide_by_zero);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGFPE)
        return failed("division by zero");
    status = child_status(invalid_instruction);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGILL)
        return failed("invalid instruction");
    return ok();
}

static int pipe_fds[2];

static void write_to_closed_pipe(void)
{
    write(pipe_fds[1], "x", 1);
}

static int test_pipe(void)
{
    pipe(pipe_fds);
    close(pipe_fds[0]);

    int status = child_status(write_to_closed_pipe);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGPIPE)
        return failed("SIGPIPE ends the writer");

    signal(SIGPIPE, SIG_IGN);
    if (write(pipe_fds[1], "x", 1) != -1 || errno != EPIPE)
        return failed("EPIPE with SIGPIPE ignored");
    return ok();
}

static int test_chld(void)
{
    handle(SIGCHLD, on_signal, 0);
    pid_t child = fork();
    if (child == 0)
        _exit(0);
    waitpid(child, NULL, 0);
    if (caught != 1 || caught_signal != SIGCHLD)
        return failed("SIGCHLD");
    return ok();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: sigtest handler|mask|kill|eintr|stop|fault|pipe|chld\n");
        return 1;
    }

    mode_name = argv[1];
    if (strcmp(argv[1], "handler") == 0)
        return test_handler();
    if (strcmp(argv[1], "mask") == 0)
        return test_mask();
    if (strcmp(argv[1], "kill") == 0)
        return test_kill();
    if (strcmp(argv[1], "eintr") == 0)
        return test_eintr();
    if (strcmp(argv[1], "stop") == 0)
        return test_stop();
    if (strcmp(argv[1], "fault") == 0)
        return test_fault();
    if (strcmp(argv[1], "pipe") == 0)
        return test_pipe();
    if (strcmp(argv[1], "chld") == 0)
        return test_chld();

    printf("sigtest: unknown mode %s\n", argv[1]);
    return 1;
}
