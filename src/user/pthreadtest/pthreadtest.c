/**
 * @file pthreadtest.c
 * @brief tests of the libc's POSIX threads.
 *
 * usage: pthreadtest <mode>, where mode is one of:
 *   create   threads start, get their argument and give back a result
 *   many     many threads, one after another and some at once
 *   detach   detached threads end and are freed
 *   exit     pthread_exit in the first thread: the process ends when the
 *            last thread ends, and the atexit handlers run
 *   mutex    a mutex keeps a counter right; the recursive and error-check
 *            types; trylock and timedlock
 *   cond     producers and consumers on a condition variable; a timed
 *            wait on CLOCK_MONOTONIC
 *   once     pthread_once runs the function once for many threads
 *   keys     thread-specific data and its destructors
 *   rwlock   readers and writers
 *   barrier  rounds of a barrier, one serial thread in each
 *   sem      semaphores: post and wait, trywait, timedwait
 *   spin     spin locks keep a counter right
 *   malloc   threads that allocate and free at once
 *   stdio    threads that write to one stream: no line is cut
 *   errno    each thread has its own errno
 *   kill     pthread_kill reaches the thread it names
 *   fork     fork in a thread that is not the first
 *   all      every mode above
 *
 * each mode prints "pthreadtest <mode>: ok", or what failed.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define THREADS 4

static const char *mode_name;

static int failed(const char *what)
{
    printf("pthreadtest %s: %s failed\n", mode_name, what);
    return 1;
}

static int ok(void)
{
    printf("pthreadtest %s: ok\n", mode_name);
    return 0;
}

/** the time now plus ms milliseconds, on a clock */
static struct timespec after_ms(clockid_t clock, long ms)
{
    struct timespec t;
    clock_gettime(clock, &t);
    t.tv_nsec += (ms % 1000) * 1000000L;
    t.tv_sec += ms / 1000 + t.tv_nsec / 1000000000L;
    t.tv_nsec %= 1000000000L;
    return t;
}

static long ms_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000L;
}

static void *double_it(void *arg)
{
    return (void *)((intptr_t)arg * 2);
}

static int test_create(void)
{
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
    {
        if (pthread_create(&threads[i], NULL, double_it, (void *)(i + 1)) != 0)
        {
            return failed("pthread_create");
        }
    }
    for (intptr_t i = 0; i < THREADS; i++)
    {
        void *result = NULL;
        if (pthread_join(threads[i], &result) != 0 || (intptr_t)result != (i + 1) * 2)
        {
            return failed("pthread_join and the result");
        }
    }
    if (!pthread_equal(pthread_self(), pthread_self()) || pthread_join(pthread_self(), NULL) != EDEADLK)
    {
        return failed("pthread_self");
    }
    return ok();
}

static int test_many(void)
{
    // more threads in all than the kernel has process slots
    for (int round = 0; round < 100; round++)
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, double_it, (void *)1) != 0 ||
            pthread_join(thread, NULL) != 0)
        {
            return failed("a thread after others ended");
        }
    }
    pthread_t threads[16];
    for (int i = 0; i < 16; i++)
    {
        if (pthread_create(&threads[i], NULL, double_it, (void *)1) != 0)
        {
            return failed("16 threads at once");
        }
    }
    for (int i = 0; i < 16; i++)
    {
        pthread_join(threads[i], NULL);
    }
    return ok();
}

static volatile int detached_ran;

static void *count_detached(void *arg)
{
    (void)arg;
    __atomic_add_fetch(&detached_ran, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static int test_detach(void)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    detached_ran = 0;
    for (int i = 0; i < 60; i++)
    {
        pthread_t thread;
        if (pthread_create(&thread, &attr, count_detached, NULL) != 0)
        {
            return failed("a detached thread");
        }
        if (i % 2)
        {
            sched_yield();
        }
    }
    // the same with pthread_detach after the start
    for (int i = 0; i < 20; i++)
    {
        pthread_t thread;
        pthread_create(&thread, NULL, count_detached, NULL);
        if (pthread_detach(thread) != 0)
        {
            return failed("pthread_detach");
        }
    }
    for (int i = 0; i < 1000 && detached_ran < 80; i++)
    {
        usleep(1000);
    }
    if (detached_ran != 80)
    {
        printf("pthreadtest detach: %d of 80 ran\n", detached_ran);
        return 1;
    }
    return ok();
}

static int exit_pipe[2];
static volatile int late_thread_done;

static void write_at_exit(void)
{
    char c = late_thread_done ? 'A' : 'x';
    write(exit_pipe[1], &c, 1);
}

static void *late_thread(void *arg)
{
    (void)arg;
    usleep(100 * 1000);
    late_thread_done = 1;
    return NULL;
}

static int test_exit(void)
{
    pipe(exit_pipe);
    pid_t pid = fork();
    if (pid == 0)
    {
        atexit(write_at_exit);
        pthread_t thread;
        pthread_create(&thread, NULL, late_thread, NULL);
        pthread_exit(NULL);
    }
    close(exit_pipe[1]);
    char c = 0;
    int status = 0;
    if (read(exit_pipe[0], &c, 1) != 1 || c != 'A')
    {
        return failed("the end after the last thread");
    }
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        return failed("the exit status");
    }
    close(exit_pipe[0]);
    return ok();
}

static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static int counter;

/** add to the counter in steps that another thread can cut into */
static void *add_locked(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5000; i++)
    {
        pthread_mutex_lock(&counter_mutex);
        int value = counter;
        if (i % 50 == 0)
        {
            sched_yield();
        }
        counter = value + 1;
        pthread_mutex_unlock(&counter_mutex);
    }
    return NULL;
}

static pthread_mutex_t held;
static volatile int other_result;

static void *try_held(void *arg)
{
    (void)arg;
    other_result = pthread_mutex_trylock(&held);
    if (other_result == 0)
    {
        pthread_mutex_unlock(&held);
        return NULL;
    }
    struct timespec until = after_ms(CLOCK_REALTIME, 50);
    if (pthread_mutex_timedlock(&held, &until) != ETIMEDOUT)
    {
        other_result = -1;
    }
    return NULL;
}

static void *unlock_held(void *arg)
{
    (void)arg;
    other_result = pthread_mutex_unlock(&held);
    return NULL;
}

static int run_other(void *(*fn)(void *))
{
    pthread_t thread;
    pthread_create(&thread, NULL, fn, NULL);
    pthread_join(thread, NULL);
    return other_result;
}

static int test_mutex(void)
{
    counter = 0;
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, add_locked, NULL);
    }
    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }
    if (counter != THREADS * 5000)
    {
        printf("pthreadtest mutex: the counter is %d, not %d\n", counter, THREADS * 5000);
        return 1;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&held, &attr);
    pthread_mutex_lock(&held);
    if (pthread_mutex_lock(&held) != EDEADLK)
    {
        return failed("an error-check mutex locked twice");
    }
    if (run_other(unlock_held) != EPERM)
    {
        return failed("an unlock by another thread");
    }
    if (run_other(try_held) != EBUSY)
    {
        return failed("trylock and timedlock on a held mutex");
    }
    pthread_mutex_unlock(&held);

    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&held, &attr);
    for (int i = 0; i < 3; i++)
    {
        if (pthread_mutex_lock(&held) != 0)
        {
            return failed("a recursive mutex");
        }
    }
    pthread_mutex_unlock(&held);
    pthread_mutex_unlock(&held);
    if (run_other(try_held) != EBUSY)
    {
        return failed("a recursive mutex still held");
    }
    pthread_mutex_unlock(&held);
    if (run_other(try_held) != 0)
    {
        return failed("a recursive mutex let go of");
    }
    return ok();
}

#define QUEUE_SIZE 8
#define ITEMS 2000

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static int queue[QUEUE_SIZE];
static int queue_len;
static long consumed_sum;

static void *produce(void *arg)
{
    (void)arg;
    for (int i = 1; i <= ITEMS; i++)
    {
        pthread_mutex_lock(&queue_mutex);
        while (queue_len == QUEUE_SIZE)
        {
            pthread_cond_wait(&not_full, &queue_mutex);
        }
        queue[queue_len++] = i;
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

static void *consume(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITEMS; i++)
    {
        pthread_mutex_lock(&queue_mutex);
        while (queue_len == 0)
        {
            pthread_cond_wait(&not_empty, &queue_mutex);
        }
        consumed_sum += queue[--queue_len];
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

static int test_cond(void)
{
    pthread_t threads[4];
    consumed_sum = 0;
    pthread_create(&threads[0], NULL, produce, NULL);
    pthread_create(&threads[1], NULL, consume, NULL);
    pthread_create(&threads[2], NULL, produce, NULL);
    pthread_create(&threads[3], NULL, consume, NULL);
    for (int i = 0; i < 4; i++)
    {
        pthread_join(threads[i], NULL);
    }
    if (consumed_sum != 2L * ITEMS * (ITEMS + 1) / 2)
    {
        return failed("producers and consumers");
    }

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_t cond;
    pthread_cond_init(&cond, &attr);
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    struct timespec until = after_ms(CLOCK_MONOTONIC, 50);
    pthread_mutex_lock(&queue_mutex);
    int err = pthread_cond_timedwait(&cond, &queue_mutex, &until);
    pthread_mutex_unlock(&queue_mutex);
    if (err != ETIMEDOUT || ms_since(&start) < 40)
    {
        return failed("a timed wait");
    }
    return ok();
}

static pthread_once_t once = PTHREAD_ONCE_INIT;
static volatile int once_ran;

static void run_once(void)
{
    usleep(20 * 1000); // the others arrive while this runs
    once_ran++;
}

static void *call_once(void *arg)
{
    (void)arg;
    pthread_once(&once, run_once);
    return (void *)(intptr_t)once_ran;
}

static int test_once(void)
{
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, call_once, NULL);
    }
    for (int i = 0; i < THREADS; i++)
    {
        void *seen = NULL;
        pthread_join(threads[i], &seen);
        if ((intptr_t)seen != 1)
        {
            return failed("a thread that returned before the function ended");
        }
    }
    return once_ran == 1 ? ok() : failed("one run");
}

static pthread_key_t key;
static volatile int destructed;

static void destruct(void *value)
{
    __atomic_add_fetch(&destructed, (int)(intptr_t)value, __ATOMIC_SEQ_CST);
}

static void *use_key(void *arg)
{
    if (pthread_getspecific(key) != NULL)
    {
        return (void *)1;
    }
    pthread_setspecific(key, arg);
    sched_yield();
    return pthread_getspecific(key) == arg ? NULL : (void *)1;
}

static int test_keys(void)
{
    if (pthread_key_create(&key, destruct) != 0)
    {
        return failed("pthread_key_create");
    }
    destructed = 0;
    pthread_setspecific(key, (void *)100);
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, use_key, (void *)(i + 1));
    }
    for (int i = 0; i < THREADS; i++)
    {
        void *bad = NULL;
        pthread_join(threads[i], &bad);
        if (bad)
        {
            return failed("a value for each thread");
        }
    }
    if (destructed != 1 + 2 + 3 + 4 || pthread_getspecific(key) != (void *)100)
    {
        return failed("the destructors");
    }
    pthread_key_delete(key);
    pthread_key_create(&key, NULL);
    if (pthread_getspecific(key) != NULL)
    {
        return failed("a new key starts as NULL");
    }
    pthread_key_delete(key);
    return ok();
}

static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
static volatile int pair[2];
static volatile int torn;

static void *rw_worker(void *arg)
{
    int writer = (int)(intptr_t)arg;
    for (int i = 0; i < 2000; i++)
    {
        if (writer)
        {
            pthread_rwlock_wrlock(&rwlock);
            pair[0]++;
            if (i % 20 == 0)
            {
                sched_yield();
            }
            pair[1]++;
        }
        else
        {
            pthread_rwlock_rdlock(&rwlock);
            if (pair[0] != pair[1])
            {
                torn = 1;
            }
        }
        pthread_rwlock_unlock(&rwlock);
    }
    return NULL;
}

static int test_rwlock(void)
{
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, rw_worker, (void *)(i % 2));
    }
    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }
    if (torn || pair[0] != 2 * 2000 || pair[1] != pair[0])
    {
        return failed("a writer alone");
    }
    pthread_rwlock_rdlock(&rwlock);
    if (pthread_rwlock_tryrdlock(&rwlock) != 0 || pthread_rwlock_trywrlock(&rwlock) != EBUSY)
    {
        return failed("trylock with readers");
    }
    pthread_rwlock_unlock(&rwlock);
    pthread_rwlock_unlock(&rwlock);
    return ok();
}

static pthread_barrier_t barrier;
static volatile int serial_count;
static volatile int arrived[3];

static void *at_barrier(void *arg)
{
    (void)arg;
    for (int round = 0; round < 3; round++)
    {
        __atomic_add_fetch(&arrived[round], 1, __ATOMIC_SEQ_CST);
        if (pthread_barrier_wait(&barrier) == PTHREAD_BARRIER_SERIAL_THREAD)
        {
            __atomic_add_fetch(&serial_count, 1, __ATOMIC_SEQ_CST);
        }
        if (arrived[round] != THREADS)
        {
            return (void *)1; // went on before the others came
        }
    }
    return NULL;
}

static int test_barrier(void)
{
    pthread_barrier_init(&barrier, NULL, THREADS);
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, at_barrier, NULL);
    }
    for (int i = 0; i < THREADS; i++)
    {
        void *early = NULL;
        pthread_join(threads[i], &early);
        if (early)
        {
            return failed("a thread that did not wait");
        }
    }
    return serial_count == 3 ? ok() : failed("one serial thread each round");
}

static sem_t items;
static sem_t spaces;
static volatile long sem_sum;

static void *sem_produce(void *arg)
{
    (void)arg;
    for (int i = 1; i <= ITEMS; i++)
    {
        sem_wait(&spaces);
        __atomic_add_fetch(&sem_sum, i, __ATOMIC_SEQ_CST);
        sem_post(&items);
    }
    return NULL;
}

static void *sem_consume(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITEMS; i++)
    {
        sem_wait(&items);
        sem_post(&spaces);
    }
    return NULL;
}

static int test_sem(void)
{
    sem_init(&items, 0, 0);
    sem_init(&spaces, 0, 4);
    sem_sum = 0;
    pthread_t a;
    pthread_t b;
    pthread_create(&a, NULL, sem_produce, NULL);
    pthread_create(&b, NULL, sem_consume, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    int value = -1;
    sem_getvalue(&spaces, &value);
    if (value != 4 || sem_sum != (long)ITEMS * (ITEMS + 1) / 2)
    {
        return failed("post and wait");
    }
    if (sem_trywait(&items) != -1 || errno != EAGAIN)
    {
        return failed("sem_trywait");
    }
    struct timespec until = after_ms(CLOCK_REALTIME, 30);
    if (sem_timedwait(&items, &until) != -1 || errno != ETIMEDOUT)
    {
        return failed("sem_timedwait");
    }
    if (sem_init(&items, 1, 0) != -1)
    {
        return failed("a shared semaphore is refused");
    }
    return ok();
}

static pthread_spinlock_t spin;

static void *add_spin(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5000; i++)
    {
        pthread_spin_lock(&spin);
        int value = counter;
        if (i % 50 == 0)
        {
            sched_yield();
        }
        counter = value + 1;
        pthread_spin_unlock(&spin);
    }
    return NULL;
}

static int test_spin(void)
{
    pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    counter = 0;
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, add_spin, NULL);
    }
    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }
    return counter == THREADS * 5000 ? ok() : failed("the counter");
}

static void *churn(void *arg)
{
    unsigned seed = (unsigned)(intptr_t)arg;
    unsigned char *blocks[32] = {0};
    size_t sizes[32] = {0};
    for (int i = 0; i < 3000; i++)
    {
        seed = seed * 1103515245u + 12345u;
        int slot = (int)((seed >> 16) % 32);
        unsigned char mark = (unsigned char)(intptr_t)arg;
        if (blocks[slot])
        {
            for (size_t j = 0; j < sizes[slot]; j++)
            {
                if (blocks[slot][j] != mark)
                {
                    return (void *)1;
                }
            }
            free(blocks[slot]);
            blocks[slot] = NULL;
            continue;
        }
        size_t size = (seed >> 8) % 4096 + 1;
        if (i % 500 == 0)
        {
            size = 200 * 1024; // a mapping of its own
        }
        blocks[slot] = malloc(size);
        if (!blocks[slot])
        {
            return (void *)1;
        }
        sizes[slot] = size;
        memset(blocks[slot], mark, size);
    }
    for (int i = 0; i < 32; i++)
    {
        free(blocks[i]);
    }
    return NULL;
}

static int test_malloc(void)
{
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, churn, (void *)(i + 1));
    }
    for (int i = 0; i < THREADS; i++)
    {
        void *bad = NULL;
        pthread_join(threads[i], &bad);
        if (bad)
        {
            return failed("memory that kept its contents");
        }
    }
    return ok();
}

static FILE *shared_file;

static void *write_lines(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < 300; i++)
    {
        fprintf(shared_file, "thread %d line %d of the test\n", id, i);
        if (i % 30 == 0)
        {
            sched_yield();
        }
    }
    return NULL;
}

static int test_stdio(void)
{
    shared_file = tmpfile();
    if (!shared_file)
    {
        return failed("tmpfile");
    }
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
    {
        pthread_create(&threads[i], NULL, write_lines, (void *)i);
    }
    for (int i = 0; i < THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }
    rewind(shared_file);
    char line[128];
    int next[THREADS] = {0};
    int lines = 0;
    while (fgets(line, sizeof(line), shared_file))
    {
        int id = -1;
        int n = -1;
        char tail[32];
        if (sscanf(line, "thread %d line %d of the %31s", &id, &n, tail) != 3 || id < 0 ||
            id >= THREADS || n != next[id] || strcmp(tail, "test") != 0)
        {
            printf("pthreadtest stdio: line %d is \"%s\"\n", lines, line);
            return 1;
        }
        next[id]++;
        lines++;
    }
    fclose(shared_file);
    return lines == THREADS * 300 ? ok() : failed("every line");
}

static void *own_errno(void *arg)
{
    int expected = (int)(intptr_t)arg;
    for (int i = 0; i < 500; i++)
    {
        if (expected == EBADF)
        {
            close(-1);
        }
        else
        {
            open("/no/such/file", O_RDONLY);
        }
        sched_yield();
        if (errno != expected)
        {
            return (void *)1;
        }
    }
    return NULL;
}

static int test_errno(void)
{
    pthread_t a;
    pthread_t b;
    pthread_create(&a, NULL, own_errno, (void *)EBADF);
    pthread_create(&b, NULL, own_errno, (void *)ENOENT);
    void *bad_a = NULL;
    void *bad_b = NULL;
    pthread_join(a, &bad_a);
    pthread_join(b, &bad_b);
    return bad_a || bad_b ? failed("errno for each thread") : ok();
}

static volatile pthread_t handled_in;
static volatile int go_on;

static void record(int sig)
{
    (void)sig;
    handled_in = pthread_self();
}

static void *wait_for_go(void *arg)
{
    (void)arg;
    while (!go_on)
    {
        sched_yield();
    }
    return NULL;
}

static int test_kill(void)
{
    signal(SIGUSR1, record);
    handled_in = 0;
    go_on = 0;
    pthread_t thread;
    pthread_create(&thread, NULL, wait_for_go, NULL);
    if (pthread_kill(thread, SIGUSR1) != 0)
    {
        return failed("pthread_kill");
    }
    for (int i = 0; i < 1000 && !handled_in; i++)
    {
        sched_yield();
    }
    go_on = 1;
    pthread_join(thread, NULL);
    sigset_t set;
    sigemptyset(&set);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
    {
        return failed("pthread_sigmask");
    }
    return handled_in == thread ? ok() : failed("the signal in that thread");
}

static void *fork_here(void *arg)
{
    (void)arg;
    pid_t pid = fork();
    if (pid == 0)
    {
        // the child: its thread works with locks and makes threads
        pthread_mutex_t mutex;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_mutex_init(&mutex, &attr);
        pthread_mutex_lock(&mutex);
        if (pthread_mutex_unlock(&mutex) != 0)
        {
            _exit(2);
        }
        pthread_t thread;
        void *result = NULL;
        if (pthread_create(&thread, NULL, double_it, (void *)21) != 0 ||
            pthread_join(thread, &result) != 0 || (intptr_t)result != 42)
        {
            _exit(3);
        }
        printf("pthreadtest fork: the child is fine\n");
        exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return (void *)(intptr_t)(WIFEXITED(status) ? WEXITSTATUS(status) : 99);
}

static int test_fork(void)
{
    fflush(stdout);
    pthread_t thread;
    pthread_create(&thread, NULL, fork_here, NULL);
    void *status = NULL;
    pthread_join(thread, &status);
    if (status)
    {
        printf("pthreadtest fork: the child ended with %d\n", (int)(intptr_t)status);
        return 1;
    }
    return ok();
}

static const struct
{
    const char *name;
    int (*run)(void);
} modes[] = {
    {"create", test_create}, {"many", test_many},   {"detach", test_detach},
    {"exit", test_exit},     {"mutex", test_mutex}, {"cond", test_cond},
    {"once", test_once},     {"keys", test_keys},   {"rwlock", test_rwlock},
    {"barrier", test_barrier}, {"sem", test_sem},   {"spin", test_spin},
    {"malloc", test_malloc}, {"stdio", test_stdio}, {"errno", test_errno},
    {"kill", test_kill},     {"fork", test_fork},
};

#define MODE_COUNT (int)(sizeof(modes) / sizeof(modes[0]))

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("usage: pthreadtest create|many|detach|exit|mutex|cond|once|keys|rwlock|barrier|"
               "sem|spin|malloc|stdio|errno|kill|fork|all\n");
        return 2;
    }
    int failures = 0;
    int found = 0;
    for (int i = 0; i < MODE_COUNT; i++)
    {
        if (strcmp(argv[1], "all") == 0 || strcmp(argv[1], modes[i].name) == 0)
        {
            mode_name = modes[i].name;
            failures += modes[i].run();
            found = 1;
        }
    }
    if (!found)
    {
        printf("pthreadtest: no mode %s\n", argv[1]);
        return 2;
    }
    return failures ? 1 : 0;
}
