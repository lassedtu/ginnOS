#include "device.h"

#include "common/string.h"
#include "kernel/sync/spinlock.h"

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

// guards registry and registry_count. devices are only ever added, and
// device_t objects are never freed, so a returned pointer stays valid.
static spinlock_t registry_lock;

/**
 * device_find with registry_lock already held.
 */
static device_t *find_locked(const char *name)
{
    for (uint32_t i = 0; i < registry_count; i++)
    {
        if (strcmp(registry[i]->name, name) == 0)
        {
            return registry[i];
        }
    }

    return NULL;
}

void device_registry_init(void)
{
    spinlock_init(&registry_lock);
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

    bool added = false;
    uint32_t flags = spin_lock_irqsave(&registry_lock);

    // reject a duplicate name so lookups stay unambiguous.
    if (registry_count < DEVICE_MAX && !find_locked(dev->name))
    {
        registry[registry_count] = dev;
        registry_count++;
        added = true;
    }

    spin_unlock_irqrestore(&registry_lock, flags);
    return added;
}

uint32_t device_count(void)
{
    uint32_t flags = spin_lock_irqsave(&registry_lock);
    uint32_t count = registry_count;
    spin_unlock_irqrestore(&registry_lock, flags);
    return count;
}

device_t *device_get(uint32_t index)
{
    device_t *dev = NULL;

    uint32_t flags = spin_lock_irqsave(&registry_lock);
    if (index < registry_count)
    {
        dev = registry[index];
    }
    spin_unlock_irqrestore(&registry_lock, flags);

    return dev;
}

device_t *device_find(const char *name)
{
    if (!name)
    {
        return NULL;
    }

    uint32_t flags = spin_lock_irqsave(&registry_lock);
    device_t *dev = find_locked(name);
    spin_unlock_irqrestore(&registry_lock, flags);

    return dev;
}
