/**
 * /dev/null and /dev/zero.
 *
 * /dev/null: reads give end of file, writes are accepted and dropped.
 * /dev/zero: reads give zero bytes, writes are accepted and dropped.
 */

#include "null.h"

#include "common/memory.h"
#include "common/string.h"
#include "kernel/device/device.h"

static uint32_t null_read(device_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    (void)buf;
    (void)len;
    return 0;
}

static uint32_t zero_read(device_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    memset(buf, 0, len);
    return len;
}

static uint32_t sink_write(device_t *dev, uint32_t offset, const void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    (void)buf;
    return len;
}

static const device_ops_t null_ops = {
    .read = null_read,
    .write = sink_write,
};

static const device_ops_t zero_ops = {
    .read = zero_read,
    .write = sink_write,
};

static void add(device_t *dev, const char *name, const device_ops_t *ops)
{
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->name[DEVICE_NAME_MAX - 1] = '\0';
    dev->type = DEVICE_TYPE_CHAR;
    dev->ops = ops;
    dev->driver_data = NULL;
    device_register(dev);
}

void null_devices_init(void)
{
    static device_t null_device;
    static device_t zero_device;
    add(&null_device, "null", &null_ops);
    add(&zero_device, "zero", &zero_ops);
}
