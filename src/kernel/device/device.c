#include "device.h"

#include "common/string.h"

/**
 * @file device.c
 * @brief flat device registry.
 *
 * a fixed array of device pointers, enough for the handful of devices ginnOS
 * has today. drivers keep their own device_t storage; the registry only holds
 * references so it can enumerate and look them up by name.
 */

#define DEVICE_MAX 32

static device_t *registry[DEVICE_MAX];
static uint32_t registry_count;

void device_registry_init(void)
{
    registry_count = 0;
    for (uint32_t i = 0; i < DEVICE_MAX; i++)
    {
        registry[i] = 0;
    }
}

bool device_register(device_t *dev)
{
    if (!dev || dev->name[0] == '\0')
    {
        return false;
    }

    if (registry_count >= DEVICE_MAX)
    {
        return false;
    }

    // reject a duplicate name so lookups stay unambiguous.
    if (device_find(dev->name))
    {
        return false;
    }

    registry[registry_count] = dev;
    registry_count++;
    return true;
}

uint32_t device_count(void)
{
    return registry_count;
}

device_t *device_get(uint32_t index)
{
    if (index >= registry_count)
    {
        return 0;
    }

    return registry[index];
}

device_t *device_find(const char *name)
{
    if (!name)
    {
        return 0;
    }

    for (uint32_t i = 0; i < registry_count; i++)
    {
        if (strcmp(registry[i]->name, name) == 0)
        {
            return registry[i];
        }
    }

    return 0;
}
