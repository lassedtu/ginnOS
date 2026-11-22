#pragma once

/**
 * @file virtio_gpu.h
 * @brief the virtio-gpu display driver.
 */

#include "common/stdint.h"

/**
 * find a virtio-gpu, take over the screen from the boot framebuffer and
 * set the size the screen wants. call after fb_init and display_init.
 * @return false if there is no virtio-gpu (the boot framebuffer stays).
 */
bool virtio_gpu_init(void);
