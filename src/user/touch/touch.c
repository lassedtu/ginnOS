// touch - create empty files

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("touch: usage: touch <file> [file...]\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++)
    {
        if (create(argv[i]) < 0)
        {
            printf("touch: cannot create '%s'\n", argv[i]);
            ret = 1;
        }
    }

    return ret;
}
