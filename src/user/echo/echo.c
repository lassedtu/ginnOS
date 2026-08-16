/**
 * @file echo.c
 * @brief Echo the command line arguments to standard output.
 */

#include <stdio.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
            putchar(' ');
        printf("%s", argv[i]);
    }
    putchar('\n');
    return 0;
}
