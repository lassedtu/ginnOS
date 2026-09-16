#pragma once

/**
 * @file fb_console.h
 * @brief framebuffer tty backend.
 *
 * builds a tty_backend_t backed by the framebuffer text grid, so the console's
 * terminal state machine can render through JetBrains Mono on the linear
 * framebuffer. only used when a framebuffer is present; otherwise the console
 * falls back to the VGA text backend.
 */

#include "kernel/tty/tty.h"

/**
 * initialize the framebuffer text grid and return a tty backend that renders
 * onto it. the framebuffer must already be initialized (fb_init).
 * @return a backend describing the framebuffer text grid.
 */
const tty_backend_t *fb_console_backend(void);
