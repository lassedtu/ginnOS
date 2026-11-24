#pragma once

/**
 * @file sys/sysmacros.h
 * @brief device numbers: a major (the driver) and a minor (the device),
 *        packed as Linux packs them in 32 bits.
 */

#define major(dev)        ((unsigned int)(((dev) >> 8) & 0xfff))
#define minor(dev)        ((unsigned int)(((dev) & 0xff) | (((dev) >> 12) & 0xfff00)))
#define makedev(ma, mi)   ((((ma) & 0xfff) << 8) | ((mi) & 0xff) | (((mi) & 0xfff00) << 12))
