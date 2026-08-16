// clear - clear the terminal screen

#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    clear_screen();
    return 0;
}
