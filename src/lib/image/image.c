/**
 * @file image.c
 * @brief load images into libgfx buffers, through stb_image.
 *
 * stb_image is built here with only what ginnOS needs: PNG, JPEG and BMP
 * from memory, no file access of its own (the file is read here), no
 * SIMD, no thread-local storage, and no HDR. (STBI_NO_LINEAR is not set:
 * in v2.30 it leaves a helper the PNG decoder needs undefined.)
 */

#include "image.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MAX_DIMENSIONS IMAGE_MAX_SIDE
#define STB_IMAGE_IMPLEMENTATION

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "../stb/stb_image.h"
#pragma GCC diagnostic pop

#define FILE_MAX_SIZE (64u * 1024 * 1024)

static void set_error(const char **error, const char *reason)
{
    if (error)
        *error = reason;
}

gfx_buffer_t *image_decode(const uint8_t *data, size_t size, const char **error)
{
    if (size == 0 || size > FILE_MAX_SIZE)
    {
        set_error(error, "bad size");
        return NULL;
    }
    int width;
    int height;
    int channels;
    uint8_t *rgba = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
    if (!rgba)
    {
        set_error(error, stbi_failure_reason());
        return NULL;
    }

    gfx_buffer_t *buf = gfx_buffer_new(width, height);
    if (!buf)
    {
        stbi_image_free(rgba);
        set_error(error, "out of memory");
        return NULL;
    }
    const uint8_t *in = rgba;
    for (int32_t y = 0; y < height; y++)
    {
        uint32_t *row = gfx_pixel_at(buf, 0, y);
        for (int32_t x = 0; x < width; x++, in += 4)
            row[x] = gfx_rgba(in[0], in[1], in[2], in[3]);
    }
    stbi_image_free(rgba);
    return buf;
}

gfx_buffer_t *image_load(const char *path, const char **error)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0)
    {
        if (fd >= 0)
            close(fd);
        set_error(error, "cannot open the file");
        return NULL;
    }
    if (st.st_size <= 0 || (size_t)st.st_size > FILE_MAX_SIZE)
    {
        close(fd);
        set_error(error, "bad size");
        return NULL;
    }

    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size);
    size_t done = 0;
    while (data && done < size)
    {
        ssize_t n = read(fd, data + done, size - done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    if (!data || done != size)
    {
        free(data);
        set_error(error, data ? "cannot read the file" : "out of memory");
        return NULL;
    }

    gfx_buffer_t *buf = image_decode(data, size, error);
    free(data);
    return buf;
}

int32_t image_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    if (in_size > INT32_MAX || out_size > INT32_MAX)
        return -1;
    return stbi_zlib_decode_buffer((char *)out, (int)out_size, (const char *)in, (int)in_size);
}
