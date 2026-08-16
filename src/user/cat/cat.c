// cat - concatenate and print files (or stdin)

#include <stdio.h>
#include <unistd.h>

#define BUF_SIZE 512

static int cat_fd(int fd)
{
    char buf[BUF_SIZE];
    int n;

    while ((n = read(fd, buf, BUF_SIZE)) > 0)
        write(1, buf, n);

    return 0;
}

static int cat_file(const char *path)
{
    int fd = open(path, 0);
    if (fd < 0)
    {
        printf("cat: %s: no such file\n", path);
        return 1;
    }

    cat_fd(fd);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        // no arguments: read from stdin
        cat_fd(0);
        return 0;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++)
    {
        if (cat_file(argv[i]) != 0)
            ret = 1;
    }

    return ret;
}
