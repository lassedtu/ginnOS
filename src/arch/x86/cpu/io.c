#include "io.h"

/** unused I/O port used for timing delays. */
#define IO_UNUSED_PORT 0x80

void io_wait(void)
{
    io_outb(IO_UNUSED_PORT, 0);
}
