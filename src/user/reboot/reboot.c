/**
 * reboot: restart the machine (root only).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>

int main(void)
{
    reboot(RB_AUTOBOOT);
    fprintf(stderr, "reboot: %s\n", strerror(errno));
    return 1;
}
