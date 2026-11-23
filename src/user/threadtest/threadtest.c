/**
 * @file threadtest.c
 * @brief Exercise the kernel's threads (clone, exit, exit_group).
 *
 * usage: threadtest <mode>
 *   ids     a thread has its own id (gettid) and the process's pid
 *   many    ten threads add to one counter in the shared memory
 *   share   the threads share the descriptors and the working directory
 *   group   _exit in a thread ends the process with that status
 *   leader  the first thread ends first: the process ends with the last
 *           thread, and with its status
 *   fault   a bad memory access in a thread kills the whole process
 *   sleep   a thread asleep in a read ends when another calls _exit
 *   fork    a thread forks; the first thread waits for that child
 *   all     every mode above
 *
 * each mode prints "threadtest <mode>: ok" when it passes. a thread does
 * not use stdio or malloc here (they are not for threads yet), and the
 * test waits for a thread by polling the id word that the kernel sets to
 * 0 when the thread ends (CLONE_CHILD_CLEARTID).
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SYSVSEM        0x00040000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000

#define STACK_SIZE (64 * 1024)

int __clone(int (*fn)(void *), void *stack, int flags, void *arg, pid_t *parent_tid, void *tls,
            pid_t *child_tid);

static const char *mode_name;

static int failed(const char *what)
{
    printf("threadtest %s: %s (errno %d)\n", mode_name, what, errno);
    return 1;
}

static int ok(void)
{
    printf("threadtest %s: ok\n", mode_name);
    return 0;
}

/**
 * start a thread that runs fn(arg). *tid holds its id until it ends.
 * @return the thread's id, or a negative errno.
 */
static pid_t start(int (*fn)(void *), void *arg, volatile pid_t *tid)
{
    char *stack = malloc(STACK_SIZE);
    if (!stack)
    {
        return -ENOMEM;
    }
    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM |
                CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
    return __clone(fn, stack + STACK_SIZE, flags, arg, (pid_t *)tid, NULL, (pid_t *)tid);
}

/** wait until the thread with this id word has ended */
static void join(volatile pid_t *tid)
{
    while (*tid)
    {
        sched_yield();
    }
}

static pid_t gettid_raw(void)
{
    return (pid_t)_syscall(SYS_GETTID, 0, 0, 0, 0, 0);
}

/** end only the calling thread (pthread_exit, without libc's help) */
static void thread_exit_raw(int status)
{
    _syscall(SYS_EXIT, status, 0, 0, 0, 0);
}

/** the wait status of a child that runs fn */
static int child_status(void (*fn)(void))
{
    pid_t pid = fork();
    if (pid == 0)
    {
        fn();
        _exit(100);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

static void spin(int rounds)
{
    for (int i = 0; i < rounds; i++)
    {
        sched_yield();
    }
}

static volatile pid_t seen_tid;
static volatile pid_t seen_pid;

static int record_ids(void *arg)
{
    (void)arg;
    seen_tid = gettid_raw();
    seen_pid = getpid();
    return 0;
}

static int test_ids(void)
{
    volatile pid_t tid = 0;
    pid_t made = start(record_ids, NULL, &tid);
    if (made <= 0)
    {
        return failed("clone");
    }
    join(&tid);
    if (seen_tid != made || seen_tid == getpid())
    {
        return failed("the thread's id");
    }
    if (seen_pid != getpid() || gettid_raw() != getpid())
    {
        return failed("getpid in the thread");
    }
    return ok();
}

#define THREADS 10
#define ROUNDS 1000

static volatile int counter;

static int add(void *arg)
{
    int step = (int)(intptr_t)arg;
    for (int i = 0; i < ROUNDS; i++)
    {
        __atomic_add_fetch(&counter, step, __ATOMIC_SEQ_CST);
        if (i % 100 == 0)
        {
            sched_yield();
        }
    }
    return 0;
}

static int test_many(void)
{
    volatile pid_t tids[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        tids[i] = 0;
        if (start(add, (void *)(intptr_t)(i + 1), &tids[i]) <= 0)
        {
            return failed("clone");
        }
    }
    for (int i = 0; i < THREADS; i++)
    {
        join(&tids[i]);
    }
    int want = ROUNDS * THREADS * (THREADS + 1) / 2;
    if (counter != want)
    {
        printf("threadtest many: counter %d, not %d\n", counter, want);
        return 1;
    }
    return ok();
}

static int pipe_fds[2];
static int spare_fd;

static int use_shared(void *arg)
{
    (void)arg;
    write(pipe_fds[1], "x", 1);
    close(spare_fd);
    chdir("/tmp");
    return 0;
}

static int test_share(void)
{
    if (pipe(pipe_fds) != 0)
    {
        return failed("pipe");
    }
    spare_fd = dup(0);
    volatile pid_t tid = 0;
    if (start(use_shared, NULL, &tid) <= 0)
    {
        return failed("clone");
    }
    join(&tid);
    char c = 0;
    char cwd[64];
    if (read(pipe_fds[0], &c, 1) != 1 || c != 'x')
    {
        return failed("a write to a shared descriptor");
    }
    if (close(spare_fd) != -1 || errno != EBADF)
    {
        return failed("a close in the thread");
    }
    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/tmp") != 0)
    {
        return failed("chdir in the thread");
    }
    chdir("/");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return ok();
}

static int exit_seven(void *arg)
{
    (void)arg;
    spin(10);
    _exit(7);
}

static void group_child(void)
{
    volatile pid_t tid = 0;
    start(exit_seven, NULL, &tid);
    for (;;)
    {
        sched_yield();
    }
}

static int test_group(void)
{
    int status = child_status(group_child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
    {
        return failed("_exit in a thread");
    }
    return ok();
}

static int return_three(void *arg)
{
    (void)arg;
    spin(20);
    return 3;
}

static void leader_child(void)
{
    volatile pid_t tid = 0;
    start(return_three, NULL, &tid);
    thread_exit_raw(0);
}

static int test_leader(void)
{
    int status = child_status(leader_child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 3)
    {
        return failed("the last thread's status");
    }
    return ok();
}

static int read_null(void *arg)
{
    (void)arg;
    return *(volatile int *)NULL;
}

static void fault_child(void)
{
    volatile pid_t tid = 0;
    start(read_null, NULL, &tid);
    for (;;)
    {
        sched_yield();
    }
}

static int test_fault(void)
{
    int status = child_status(fault_child);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
    {
        return failed("SIGSEGV in a thread");
    }
    return ok();
}

static int read_forever(void *arg)
{
    (void)arg;
    char c;
    return (int)read(pipe_fds[0], &c, 1); // nobody writes
}

static void sleep_child(void)
{
    pipe(pipe_fds);
    volatile pid_t tid = 0;
    start(read_forever, NULL, &tid);
    spin(20);
    _exit(5);
}

static int test_sleep(void)
{
    int status = child_status(sleep_child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 5)
    {
        return failed("_exit with a thread asleep");
    }
    return ok();
}

static volatile pid_t forked;

static int fork_child(void *arg)
{
    (void)arg;
    pid_t pid = fork();
    if (pid == 0)
    {
        _exit(9);
    }
    forked = pid;
    return 0;
}

static int test_fork(void)
{
    volatile pid_t tid = 0;
    if (start(fork_child, NULL, &tid) <= 0)
    {
        return failed("clone");
    }
    join(&tid);
    int status = 0;
    if (forked <= 0 || waitpid(forked, &status, 0) != forked || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 9)
    {
        return failed("waitpid for a thread's child");
    }
    return ok();
}

static const struct
{
    const char *name;
    int (*run)(void);
} modes[] = {
    {"ids", test_ids},       {"many", test_many},   {"share", test_share},
    {"group", test_group},   {"leader", test_leader}, {"fault", test_fault},
    {"sleep", test_sleep},   {"fork", test_fork},
};

#define MODE_COUNT (sizeof(modes) / sizeof(modes[0]))

int main(int argc, char **argv)
{
    if (argc == 2)
    {
        int result = 0;
        bool all = strcmp(argv[1], "all") == 0;
        bool found = all;
        for (size_t i = 0; i < MODE_COUNT; i++)
        {
            if (all || strcmp(argv[1], modes[i].name) == 0)
            {
                mode_name = modes[i].name;
                result |= modes[i].run();
                found = true;
            }
        }
        if (found)
        {
            return result;
        }
    }
    printf("usage: threadtest ids|many|share|group|leader|fault|sleep|fork|all\n");
    return 2;
}
