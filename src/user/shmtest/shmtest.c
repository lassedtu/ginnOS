/**
 * @file shmtest.c
 * @brief test shared memory (shm_create and mmap of its fd).
 *
 * usage: shmtest
 *
 * checks, in order:
 *   two   two mappings of one object in one process see the same memory
 *   fork  after fork, a write in the child is seen by the parent, through
 *         the inherited mapping and through a new mapping of the fd
 *   prot  mprotect on a shared mapping keeps it shared across fork
 *   close a mapping stays valid after its fd is closed
 *   errors size 0, a mapping past the end of the object
 *   free  the frames are freed: the checks run in a child process 4
 *         times, and free memory is the same after runs 2, 3 and 4 (a
 *         leak would lose memory on every run)
 *
 * prints "shmtest: ok" when all pass.
 */

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIZE (64 * 1024)

static int failed(const char *what)
{
    printf("shmtest: %s failed (errno %d)\n", what, errno);
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

static char *map(int fd, size_t length, int prot)
{
    char *p = mmap(NULL, length, prot, MAP_SHARED, fd, 0);
    return p == MAP_FAILED ? NULL : p;
}

/**
 * run fn in a child and wait for it.
 * @return the child's exit status, or -1.
 */
static int in_child(int (*fn)(int fd, char *mem), int fd, char *mem)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
        _exit(fn(fd, mem));
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

static int child_writes(int fd, char *mem)
{
    // through the inherited mapping
    strcpy(mem + 4096, "from the child");
    // through a new mapping of the inherited fd
    char *again = map(fd, SIZE, PROT_READ | PROT_WRITE);
    if (!again || strcmp(again, "hello") != 0)
        return 1;
    strcpy(again + 8192, "second mapping");
    return 0;
}

static int child_writes_once(int fd, char *mem)
{
    (void)fd;
    mem[SIZE - 1] = 42;
    return 0;
}

/**
 * every check but the last: in a child process, so its page tables are
 * freed before the parent counts the free memory.
 */
static int run_checks(void)
{
    int fd = shm_create(SIZE, 0);
    if (fd < 0)
        return failed("shm_create");

    // two: two mappings in one process
    char *a = map(fd, SIZE, PROT_READ | PROT_WRITE);
    char *b = map(fd, SIZE, PROT_READ | PROT_WRITE);
    if (!a || !b || a == b)
        return failed("two mappings");
    for (int i = 0; i < SIZE; i++)
    {
        if (a[i] != 0)
            return failed("zeroed memory");
    }
    strcpy(a, "hello");
    if (strcmp(b, "hello") != 0)
        return failed("a write seen through the other mapping");

    // fork: the child's writes reach the parent
    if (in_child(child_writes, fd, a) != 0)
        return failed("the child");
    if (strcmp(a + 4096, "from the child") != 0 || strcmp(b + 8192, "second mapping") != 0)
        return failed("the child's writes seen by the parent");

    // prot: mprotect keeps the mapping shared
    if (mprotect(a, SIZE, PROT_READ | PROT_WRITE) != 0)
        return failed("mprotect");
    if (in_child(child_writes_once, fd, a) != 0 || b[SIZE - 1] != 42)
        return failed("sharing after mprotect");

    // close: the mappings stay
    close(fd);
    a[100] = 7;
    if (b[100] != 7)
        return failed("a mapping after close");
    munmap(a, SIZE);
    munmap(b, SIZE);

    // errors
    errno = 0;
    if (shm_create(0, 0) >= 0 || errno != EINVAL)
        return failed("size 0 is EINVAL");
    int small = shm_create(4096, 0);
    if (small < 0 || mmap(NULL, 8192, PROT_READ, MAP_SHARED, small, 0) != MAP_FAILED)
        return failed("a mapping past the end");
    close(small);
    return 0;
}

/**
 * run the checks in a child process and wait for it.
 * @return true if they passed.
 */
static bool checks_in_child(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0)
        _exit(run_checks());
    int status;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
    // run the checks 4 times. the first runs can leave pages behind for
    // good reasons (the kernel heap grows once, the parent's stack); after
    // that, a leak would lose memory on every run
    long free_after[4];
    for (int run = 0; run < 4; run++)
    {
        if (!checks_in_child())
            return 1; // the child printed what failed
        free_after[run] = free_kb();
    }
    if (free_after[1] < 0 || free_after[2] != free_after[1] || free_after[3] != free_after[1])
    {
        printf("shmtest: free memory after each run: %ld %ld %ld %ld kB\n", free_after[0],
               free_after[1], free_after[2], free_after[3]);
        return failed("freeing the frames");
    }

    printf("shmtest: ok\n");
    return 0;
}
