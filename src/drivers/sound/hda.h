#pragma once

/**
 * @file hda.h
 * @brief Intel High Definition Audio output (PCI class 04:03; almost every
 * PC since 2004; QEMU -device intel-hda -device hda-output).
 *
 * the driver resets the controller, finds the codecs, and in the first
 * codec with an analog output it sets up a path from a converter (DAC) to
 * each connected output pin: line out, speakers and headphones. one output
 * stream feeds all of them. it registers with the audio core (/dev/dsp).
 */

#include "common/stdint.h"

/**
 * find the first HDA controller and register it as the sound device.
 * call after pci_init and apic_init.
 * @return true if a controller is in use.
 */
bool hda_init(void);
