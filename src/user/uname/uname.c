/**
 * @file uname.c
 * @brief print the name of the system.
 *
 * usage: uname [-asnrvm]
 *
 *   -s  the system's name (without options too)
 *   -n  the host's name
 *   -r  the release
 *   -v  the version: the commit and its date
 *   -m  the machine
 *   -a  all of them
 */

#include <stdio.h>
#include <sys/utsname.h>

int main(int argc, char **argv)
{
    struct utsname u;
    if (uname(&u) != 0)
    {
        perror("uname");
        return 1;
    }
    const char *fields[] = {u.sysname, u.nodename, u.release, u.version, u.machine};
    const char *letters = "snrvm";
    int want = 0;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            fprintf(stderr, "usage: uname [-asnrvm]\n");
            return 1;
        }
        for (const char *c = argv[i] + 1; *c; c++)
        {
            if (*c == 'a')
            {
                want = 0x1F;
                continue;
            }
            int bit = -1;
            for (int j = 0; letters[j]; j++)
                if (letters[j] == *c)
                    bit = j;
            if (bit < 0)
            {
                fprintf(stderr, "uname: no option -%c\n", *c);
                return 1;
            }
            want |= 1 << bit;
        }
    }
    if (!want)
        want = 1;
    const char *space = "";
    for (int i = 0; i < 5; i++)
    {
        if (want & (1 << i))
        {
            printf("%s%s", space, fields[i]);
            space = " ";
        }
    }
    printf("\n");
    return 0;
}
