<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# image

libimage: PNG, JPEG and BMP files.

## &lt;image.h&gt;

Source: [`src/lib/image/include/image.h`](https://github.com/lassedtu/ginnOS/blob/main/src/lib/image/include/image.h)

load PNG, JPEG and BMP files into libgfx buffers.

the decoder is stb\_image (public domain, src/lib/stb). the result is a
gfx\_buffer\_t of premultiplied ARGB; an image without alpha is opaque.

### image\_load

```c
gfx_buffer_t *image_load(const char *path, const char **error);
```

load an image file.

- `error`: receives a short reason on failure (may be NULL).

Returns: a new buffer (free it with gfx\_buffer\_free), or NULL.

### image\_decode

```c
gfx_buffer_t *image_decode(const uint8_t *data, size_t size, const char **error);
```

decode an image from memory.

### image\_inflate

```c
int32_t image_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);
```

decompress zlib data (RFC 1950) into a buffer of known size.

Returns: the bytes written, or -1 (bad data, or out does not hold it).
