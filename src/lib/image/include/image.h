#pragma once

/**
 * @file image.h
 * @brief load PNG, JPEG and BMP files into libgfx buffers.
 *
 * the decoder is stb_image (public domain, src/lib/stb). the result is a
 * gfx_buffer_t of premultiplied ARGB; an image without alpha is opaque.
 */

#include <gfx.h>

// images larger than this on either side are refused
#define IMAGE_MAX_SIDE 8192

/**
 * load an image file.
 * @param error receives a short reason on failure (may be NULL).
 * @return a new buffer (free it with gfx_buffer_free), or NULL.
 */
gfx_buffer_t *image_load(const char *path, const char **error);

/**
 * decode an image from memory.
 */
gfx_buffer_t *image_decode(const uint8_t *data, size_t size, const char **error);
