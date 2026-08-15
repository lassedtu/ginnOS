// echo - print a newline
// TODO: argument passing from shell to child process not yet implemented.
// once argv is available, this will print all arguments separated by spaces.

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    write(1, "\n", 1);
    return 0;
}
