/**
 * reboot: restart the machine (root only).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    reboot();
    fprintf(stderr, "reboot: %s\n", strerror(errno));
    return 1;
}
