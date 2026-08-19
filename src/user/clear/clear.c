// clear - clear the terminal screen

#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    // ESC[2J = clear entire screen, ESC[H = cursor to home (1,1)
    const char seq[] = {27, '[', '2', 'J', 27, '[', 'H'};
    write(1, seq, 7);
    return 0;
}
