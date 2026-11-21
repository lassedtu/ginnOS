#pragma once

/**
 * @file ac97.h
 * @brief AC'97 sound output (Intel ICH style controllers, PCI class
 * 04:01; QEMU -device AC97).
 *
 * the controller has two I/O port ranges: the codec mixer (NAM, BAR 0)
 * and the bus master (NABM, BAR 1). the driver plays 16-bit stereo through
 * the PCM out channel and registers it with the audio core (/dev/dsp).
 */

#include "common/stdint.h"

/**
 * find the first AC'97 controller and register it as the sound device.
 * call after pci_init and apic_init.
 * @return true if a controller is in use.
 */
bool ac97_init(void);
