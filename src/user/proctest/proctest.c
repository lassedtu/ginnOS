/**
 * @file proctest.c
 * @brief Exercise the kernel's process model.
 *
 * usage: proctest <mode>
 *   fork   fork, write to a global, the heap, the stack and an mmap in the
 *          child, and check that the parent still sees its own values
 *          (copy-on-write)
 *   pipe   fork with a pipe between parent and child
 *   atomic two children write 4 KiB blocks into one pipe at the same time;
 *          every block must arrive in one piece (PIPE_BUF)
 *   many   fork until the process table is full, then reap every child
 *   wait   waitpid with WNOHANG and pid -1, ECHILD, the status of a child
 *          killed by SIGSEGV, and getppid
 *   exec   execve a missing program (must fail and return), then fork and
 *          execve this program in the child with its own argv and
 *          environment (the child checks them in mode "execed")
 *
 * each mode prints "proctest <mode>: ok" when it passes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

#define MANY_MAX 100
#define BLOCK_SIZE 4096
#define BLOCKS_PER_WRITER 16

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

    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 7)
    {
        printf("proctest fork: child failed (status 0x%x)\n", status);
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
    waitpid(child, NULL, 0);

    if (strcmp(buffer, "hello from the child") != 0)
    {
        printf("proctest pipe: read \"%s\"\n", buffer);
        return 1;
    }

    printf("proctest pipe: ok\n");
    return 0;
}

/**
 * in a child: write BLOCKS_PER_WRITER blocks filled with value, then exit.
 */
static void write_blocks(int fd, char value)
{
    static char block[BLOCK_SIZE];
    memset(block, value, BLOCK_SIZE);
    for (int i = 0; i < BLOCKS_PER_WRITER; i++)
    {
        if (write(fd, block, BLOCK_SIZE) != BLOCK_SIZE)
            _exit(1);
    }
    _exit(0);
}

static int test_atomic(void)
{
    int fds[2];
    if (pipe(fds) != 0)
    {
        printf("proctest atomic: pipe failed\n");
        return 1;
    }

    pid_t writers[2];
    for (int w = 0; w < 2; w++)
    {
        writers[w] = fork();
        if (writers[w] == 0)
        {
            close(fds[0]);
            write_blocks(fds[1], (char)('A' + w));
        }
    }
    close(fds[1]);

    // read the whole stream; every BLOCK_SIZE run must hold one value
    static char block[BLOCK_SIZE];
    int blocks[2] = {0, 0};
    int filled = 0;
    int n;
    while ((n = read(fds[0], block + filled, BLOCK_SIZE - filled)) > 0)
    {
        filled += n;
        if (filled < BLOCK_SIZE)
            continue;
        filled = 0;
        for (int i = 1; i < BLOCK_SIZE; i++)
        {
            if (block[i] != block[0])
            {
                printf("proctest atomic: block mixes '%c' and '%c'\n", block[0], block[i]);
                return 1;
            }
        }
        if (block[0] == 'A' || block[0] == 'B')
            blocks[block[0] - 'A']++;
    }
    close(fds[0]);

    for (int w = 0; w < 2; w++)
        waitpid(writers[w], NULL, 0);

    if (filled != 0 || blocks[0] != BLOCKS_PER_WRITER || blocks[1] != BLOCKS_PER_WRITER)
    {
        printf("proctest atomic: got %d and %d blocks\n", blocks[0], blocks[1]);
        return 1;
    }

    printf("proctest atomic: ok\n");
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
        int status;
        if (waitpid(children[i], &status, 0) != children[i] || status != 0)
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

static int test_wait(void)
{
    pid_t self = getpid();
    int fds[2];
    pipe(fds);

    // a child that waits for the parent's signal (a byte on the pipe), then
    // checks getppid
    pid_t slow = fork();
    if (slow == 0)
    {
        char byte;
        close(fds[1]);
        read(fds[0], &byte, 1);
        _exit(getppid() == self ? 3 : 1);
    }
    close(fds[0]);

    // a child killed by a bad memory access
    pid_t crash = fork();
    if (crash == 0)
    {
        volatile int *null = NULL;
        _exit(*null);
    }

    int status;
    if (waitpid(slow, &status, WNOHANG) != 0)
    {
        printf("proctest wait: WNOHANG did not return 0\n");
        return 1;
    }

    // the first child to exit is the crashing one: the other is waiting
    pid_t first = waitpid(-1, &status, 0);
    if (first != crash || !WIFSIGNALED(status) || WTERMSIG(status) != 11)
    {
        printf("proctest wait: got pid %d status 0x%x\n", first, status);
        return 1;
    }

    write(fds[1], "x", 1);
    close(fds[1]);
    if (wait(&status) != slow || !WIFEXITED(status) || WEXITSTATUS(status) != 3)
    {
        printf("proctest wait: slow child status 0x%x\n", status);
        return 1;
    }

    if (wait(&status) != -1 || errno != ECHILD)
    {
        printf("proctest wait: no ECHILD without children\n");
        return 1;
    }

    printf("proctest wait: ok\n");
    return 0;
}

static int test_exec(void)
{
    char *missing_argv[] = {"missing", NULL};
    if (execve("/bin/no-such-program", missing_argv, environ) != -1)
    {
        printf("proctest exec: execve of a missing program did not fail\n");
        return 1;
    }

    pid_t child = fork();
    if (child < 0)
    {
        printf("proctest exec: fork failed\n");
        return 1;
    }
    if (child == 0)
    {
        char *child_argv[] = {"proctest", "execed", "second argument", NULL};
        char *child_envp[] = {"PROCTEST=42", "OTHER=x", NULL};
        execve("/bin/proctest", child_argv, child_envp);
        _exit(99); // execve failed
    }

    int status;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
    {
        printf("proctest exec: child status 0x%x\n", status);
        return 1;
    }

    printf("proctest exec: ok\n");
    return 0;
}

/**
 * the program execve'd by test_exec: check argv and the environment.
 */
static int test_execed(int argc, char **argv)
{
    const char *value = getenv("PROCTEST");
    if (argc != 3 || strcmp(argv[2], "second argument") != 0 || !value ||
        strcmp(value, "42") != 0 || getenv("MISSING") != NULL)
    {
        return 1;
    }
    return atoi(value);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: proctest fork|pipe|atomic|many|wait|exec\n");
        return 1;
    }

    if (strcmp(argv[1], "fork") == 0)
        return test_fork();
    if (strcmp(argv[1], "pipe") == 0)
        return test_pipe();
    if (strcmp(argv[1], "atomic") == 0)
        return test_atomic();
    if (strcmp(argv[1], "many") == 0)
        return test_many();
    if (strcmp(argv[1], "wait") == 0)
        return test_wait();
    if (strcmp(argv[1], "exec") == 0)
        return test_exec();
    if (strcmp(argv[1], "execed") == 0)
        return test_execed(argc, argv);

    printf("proctest: unknown mode %s\n", argv[1]);
    return 1;
}
