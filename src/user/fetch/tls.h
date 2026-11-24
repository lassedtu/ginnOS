#pragma once

/**
 * @file tls.h
 * @brief fetch's TLS connections, over mbedTLS.
 */

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define TLS_CA_BUNDLE "/etc/ssl/certs/ca-certificates.crt"

typedef struct tls tls_t;

/**
 * the TLS handshake on a connected socket.
 * @param host the server's name: sent to it (SNI), and its certificate
 *        must be for it.
 * @param verify check the server's certificate against TLS_CA_BUNDLE and
 *        the clock (false: any certificate is taken).
 * @param error receives a message when it fails.
 * @return the connection, or NULL.
 */
tls_t *tls_open(int fd, const char *host, bool verify, char *error, size_t error_size);

/**
 * read decrypted data.
 * @return the bytes read, 0 at the end of the stream, or -1 (the message
 *         is in tls_error).
 */
ssize_t tls_read(tls_t *tls, void *buffer, size_t length);

/**
 * write all of the data.
 * @return false on an error (the message is in tls_error).
 */
bool tls_write(tls_t *tls, const void *data, size_t length);

/**
 * the message of the last error.
 */
const char *tls_error(const tls_t *tls);

/**
 * tell the server the connection ends, and free it (the socket stays
 * open).
 */
void tls_close(tls_t *tls);
