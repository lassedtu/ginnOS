#pragma once

/**
 * @file ze_compat.h
 * @brief force-included into ze's ginnOS platform layer.
 *
 * zen-editor (a submodule) opens files with open(path, 0) for reading and
 * for writing, from before ginnOS checked access modes. 0 is O_RDONLY, so
 * its writes would now fail with EBADF. until zen-editor passes real flags
 * (O_RDONLY to load, O_WRONLY | O_CREAT | O_TRUNC to save), its open calls
 * use O_RDWR.
 */

#include <fcntl.h>

static inline int ze_compat_open(const char *path, int flags)
{
    return open(path, flags == 0 ? O_RDWR : flags, 0644);
}

#define open(path, flags) ze_compat_open(path, flags)
