// pwd - print working directory

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char buf[256];

    if (getcwd(buf, sizeof(buf)) == 0)
        printf("%s\n", buf);
    else
        printf("pwd: cannot get current directory\n");

    return 0;
}
