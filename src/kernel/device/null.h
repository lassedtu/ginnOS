#pragma once

/**
 * @file null.h
 * @brief /dev/null and /dev/zero.
 */

/**
 * register the null and zero devices. call after device_registry_init.
 */
void null_devices_init(void);
