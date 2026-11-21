#pragma once

/**
 * @file xhci.h
 * @brief USB keyboards and pointing devices through xHCI controllers
 * (PCI class 0C:03:30).
 *
 * xhci_init takes each xHCI controller from the firmware, resets it, and
 * looks at the devices on its root ports. a keyboard or a mouse that
 * speaks the HID boot protocol, or a pointing device whose report
 * descriptor has x and y (QEMU's tablet), gets its interrupt endpoint
 * configured. the controller interrupt (or, without one, the timer) reads
 * its events. keyboard reports become PS/2 scan codes for the keyboard
 * driver, so the layout and the key events are the same as for a PS/2
 * keyboard; buttons, motion and the wheel go to /dev/input. hubs and
 * other devices are not supported.
 */

#include "common/stdint.h"

/**
 * set up the xHCI controllers and their input devices. call after
 * pci_init and mmio_init, with interrupts off.
 * @return the number of input devices found.
 */
uint32_t xhci_init(void);
