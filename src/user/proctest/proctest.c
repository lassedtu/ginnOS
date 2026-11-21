/**
 * @file proctest.c
 * @brief Exercise the kernel's process model.
 *
 * usage: proctest <mode>
 *   fork   fork, write to a global, the heap, the stack and an mmap in the
 *          child, and check that the parent still sees its own values
 *          (copy-on-write)
 *   pipe   fork with a pipe between parent and child
 *   many   fork until the process table is full, then reap every child
 *
 * each mode prints "proctest <mode>: ok" when it passes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MANY_MAX 100

static int global_value = 1;

static int test_fork(void)
{
    int stack_value = 2;
    int *heap_value = malloc(sizeof(int));
    int *mmap_value = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (!heap_value || mmap_value == MAP_FAILED)
    {
        printf("proctest fork: allocation failed\n");
        return 1;
    }
    *heap_value = 3;
    *mmap_value = 4;

    pid_t child = fork();
    if (child < 0)
    {
        printf("proctest fork: fork failed\n");
        return 1;
    }

    if (child == 0)
    {
        // the child sees the parent's values, then changes its own copies
        int seen = global_value == 1 && stack_value == 2 && *heap_value == 3 && *mmap_value == 4;
        global_value = 10;
        stack_value = 20;
        *heap_value = 30;
        *mmap_value = 40;
        int changed =
            global_value == 10 && stack_value == 20 && *heap_value == 30 && *mmap_value == 40;
        _exit(seen && changed ? 7 : 1);
    }

    int status = waitpid(child);
    if (status != 7)
    {
        printf("proctest fork: child failed (status %d)\n", status);
        return 1;
    }
    if (global_value != 1 || stack_value != 2 || *heap_value != 3 || *mmap_value != 4)
    {
        printf("proctest fork: the child's writes reached the parent\n");
        return 1;
    }

    // the parent can still write to its pages (no longer shared)
    global_value = 5;
    *heap_value = 6;
    if (global_value != 5 || *heap_value != 6)
    {
        printf("proctest fork: parent write lost\n");
        return 1;
    }

    printf("proctest fork: ok\n");
    return 0;
}

static int test_pipe(void)
{
    int fds[2];
    if (pipe(fds) != 0)
    {
        printf("proctest pipe: pipe failed\n");
        return 1;
    }

    pid_t child = fork();
    if (child < 0)
    {
        printf("proctest pipe: fork failed\n");
        return 1;
    }

    if (child == 0)
    {
        close(fds[0]);
        const char *message = "hello from the child";
        write(fds[1], message, strlen(message));
        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    char buffer[64];
    int total = 0;
    int n;
    while ((n = read(fds[0], buffer + total, sizeof(buffer) - 1 - total)) > 0)
    {
        total += n;
    }
    buffer[total] = '\0';
    close(fds[0]);
    waitpid(child);

    if (strcmp(buffer, "hello from the child") != 0)
    {
        printf("proctest pipe: read \"%s\"\n", buffer);
        return 1;
    }

    printf("proctest pipe: ok\n");
    return 0;
}

static int test_many(void)
{
    pid_t children[MANY_MAX];
    int count = 0;
    int fds[2];

    // the children wait on a pipe, so they all exist at the same time
    if (pipe(fds) != 0)
    {
        printf("proctest many: pipe failed\n");
        return 1;
    }

    while (count < MANY_MAX)
    {
        pid_t child = fork();
        if (child < 0)
        {
            break; // the process table is full
        }
        if (child == 0)
        {
            char byte;
            close(fds[1]);
            read(fds[0], &byte, 1); // returns at EOF, when the parent closes
            _exit(0);
        }
        children[count++] = child;
    }

    close(fds[0]);
    close(fds[1]);

    int failed = 0;
    for (int i = 0; i < count; i++)
    {
        if (waitpid(children[i]) != 0)
        {
            failed++;
        }
    }

    if (count == MANY_MAX || count == 0 || failed != 0)
    {
        printf("proctest many: %d children, %d failed\n", count, failed);
        return 1;
    }

    printf("proctest many: %d children, ok\n", count);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: proctest fork|pipe|many\n");
        return 1;
    }

    if (strcmp(argv[1], "fork") == 0)
        return test_fork();
    if (strcmp(argv[1], "pipe") == 0)
        return test_pipe();
    if (strcmp(argv[1], "many") == 0)
        return test_many();

    printf("proctest: unknown mode %s\n", argv[1]);
    return 1;
}
