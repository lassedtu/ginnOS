#pragma once

/**
 * @file xhci.h
 * @brief USB keyboards through xHCI controllers (PCI class 0C:03:30).
 *
 * xhci_init takes each xHCI controller from the firmware, resets it, and
 * looks at the devices on its root ports. a keyboard that speaks the HID
 * boot protocol gets its interrupt endpoint configured. the controller
 * interrupt (or, without one, the timer) reads its events; each report
 * becomes PS/2 scan codes for the keyboard driver, so the keyboard layout
 * and the key events are the same as for a PS/2 keyboard. hubs and other devices are
 * not supported.
 */

#include "common/stdint.h"

/**
 * set up the xHCI controllers and their keyboards. call after pci_init and
 * mmio_init, with interrupts off.
 * @return the number of keyboards found.
 */
uint32_t xhci_init(void);
