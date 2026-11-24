#pragma once

/**
 * @file virtio_net.h
 * @brief the virtio network device (virtio 1.0, section 5.1).
 */

#include "common/stdint.h"

/**
 * find a virtio network device, set it up and register it as eth0.
 * @return false if there is none or it cannot be set up.
 */
bool virtio_net_init(void);
