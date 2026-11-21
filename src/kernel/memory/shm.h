#pragma once

/**
 * @file shm.h
 * @brief shared memory objects: pages that several processes map.
 *
 * shm_create makes an object of zeroed pages and an open file for it. a
 * process maps it with mmap(fd); every mapping of the object, in any
 * process, uses the same frames, so a write in one is seen in all. this
 * is how a window's pixels go from an app to the compositor.
 *
 * the object lives while its open file does (the file can be shared by
 * fork and dup, and later sent over a socket). each mapping holds its own
 * reference to the frames, so a mapping stays valid after the file is
 * closed, and the frames are freed when the last file and the last
 * mapping are gone.
 */

#include "common/error.h"
#include "common/stdint.h"

struct file;

// the largest object
#define SHM_MAX_SIZE (64u * 1024 * 1024)

/**
 * make a shared memory object of size bytes (rounded up to whole pages).
 * @param out receives the open file (one reference), opened read-write.
 * @return KERR_OK, KERR_INVAL (size 0 or too big) or KERR_NOMEM.
 */
kerr_t shm_create(size_t size, struct file **out);

/**
 * true if the open file is a shared memory object (its mappings are
 * VMA_SHARED areas).
 */
bool shm_is_file(const struct file *file);
