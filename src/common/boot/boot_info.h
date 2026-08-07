#include "../stdint.h"

/**
 * structure containing information passed from the bootloader to the kernel.
 */
typedef struct
{
    uint8_t boot_drive; // the BIOS drive number of the boot drive (e.g., 0x80 for the first hard disk).

} boot_info_t;