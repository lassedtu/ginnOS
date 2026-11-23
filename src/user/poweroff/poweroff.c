/**
 * poweroff: turn the machine off (root only).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>

int main(void)
{
    reboot(RB_POWER_OFF);
    fprintf(stderr, "poweroff: %s\n", strerror(errno));
    return 1;
}
