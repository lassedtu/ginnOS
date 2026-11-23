#pragma once

/**
 * @file reboot.h
 * @brief restart or turn off the machine (root only), as on Linux.
 */

#define RB_AUTOBOOT  0x01234567 // restart
#define RB_POWER_OFF 0x4321FEDC // turn the machine off

/**
 * restart or turn off the machine.
 * @param how RB_AUTOBOOT or RB_POWER_OFF.
 * @return does not return on success; -1 with errno EPERM for other users,
 *         EINVAL for another value.
 */
int reboot(int how);
