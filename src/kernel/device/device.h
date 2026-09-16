#pragma once

/**
 * @file device.h
 * @brief generic device model: a named/typed handle plus a registry.
 *
 * drivers wrap their concrete device in a device_t and register it here during
 * init, so the rest of the kernel has one place to enumerate what hardware is
 * present. this is the groundwork for a future devfs that surfaces registered
 * devices as /dev/hda, /dev/tty0, and so on.
 *
 * the model intentionally stays thin: it does not replace the driver-specific
 * init flow (which has its own ordering), it just records the result.
 */

#include "common/stdint.h"

// broad device classes, enough to group the registry today.
typedef enum
{
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_BLOCK,   // block storage (disk, partition)
    DEVICE_TYPE_CHAR,    // character stream (tty, serial)
    DEVICE_TYPE_INPUT,   // input source (keyboard)
    DEVICE_TYPE_TIMER,   // timer/clock source
} device_type_t;

// longest device name the registry stores (including the null terminator).
#define DEVICE_NAME_MAX 16

struct device;
typedef struct device device_t;

/**
 * optional per-device operations. all fields may be NULL; the model does not
 * require any of them. concrete device families extend behaviour through their
 * own interfaces (block_device_t, tty_t, ...) and use this only for lifecycle.
 */
typedef struct
{
    void (*shutdown)(device_t *dev); // quiesce the device (optional)
} device_ops_t;

/**
 * a registered device.
 */
struct device
{
    char name[DEVICE_NAME_MAX]; // stable name, e.g. "hda", "tty0", "kbd"
    device_type_t type;         // device class
    const device_ops_t *ops;    // optional lifecycle ops (may be NULL)
    void *driver_data;          // the concrete device the driver owns
};

/**
 * reset the device registry. call once during early boot.
 */
void device_registry_init(void);

/**
 * register a device. the device_t is owned by the caller and must outlive its
 * registration (drivers use static or long-lived storage).
 * @param dev the device to add.
 * @return true on success, false if the registry is full or dev is invalid.
 */
bool device_register(device_t *dev);

/**
 * number of currently registered devices.
 */
uint32_t device_count(void);

/**
 * get a registered device by index (0 .. device_count()-1).
 * @return the device, or NULL if the index is out of range.
 */
device_t *device_get(uint32_t index);

/**
 * find a registered device by name.
 * @return the device, or NULL if no device has that name.
 */
device_t *device_find(const char *name);
