/**
 * @file tls.c
 * @brief fetch's TLS connections (see tls.h).
 *
 * mbedTLS reads and writes the socket through bio_send and bio_receive.
 * TLS 1.3 needs the PSA crypto layer, so psa_crypto_init comes first.
 */

#include "tls.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

// what bio_send and bio_receive give when the socket fails (errno has why)
#define SOCKET_FAILED (-0x7F00)

struct tls
{
    int fd;
    int socket_errno;
    char error[256];
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt authorities;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
};

static int bio_send(void *context, const unsigned char *data, size_t length)
{
    tls_t *tls = context;
    ssize_t n = write(tls->fd, data, length);
    if (n >= 0)
    {
        return (int)n;
    }
    if (errno == EINTR)
    {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    tls->socket_errno = errno;
    return SOCKET_FAILED;
}

static int bio_receive(void *context, unsigned char *buffer, size_t length)
{
    tls_t *tls = context;
    ssize_t n = read(tls->fd, buffer, length);
    if (n >= 0)
    {
        return (int)n; // 0: the server closed the connection
    }
    if (errno == EINTR)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    tls->socket_errno = errno;
    return SOCKET_FAILED;
}

/**
 * put the message for an mbedTLS error code into tls->error.
 */
static void set_error(tls_t *tls, int code)
{
    if (code == SOCKET_FAILED)
    {
        snprintf(tls->error, sizeof(tls->error), "%s", strerror(tls->socket_errno));
        return;
    }
    char text[200];
    mbedtls_strerror(code, text, sizeof(text));
    snprintf(tls->error, sizeof(tls->error), "%s", text);
}

static void tls_free(tls_t *tls)
{
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    mbedtls_x509_crt_free(&tls->authorities);
    mbedtls_ctr_drbg_free(&tls->random);
    mbedtls_entropy_free(&tls->entropy);
    free(tls);
}

/**
 * fail tls_open: copy the message out and free the connection.
 */
static tls_t *open_failed(tls_t *tls, char *error, size_t error_size)
{
    snprintf(error, error_size, "%s", tls->error);
    tls_free(tls);
    return NULL;
}

tls_t *tls_open(int fd, const char *host, bool verify, char *error, size_t error_size)
{
    if (psa_crypto_init() != PSA_SUCCESS)
    {
        snprintf(error, error_size, "the crypto library did not start");
        return NULL;
    }
    tls_t *tls = calloc(1, sizeof(*tls));
    if (!tls)
    {
        snprintf(error, error_size, "%s", strerror(ENOMEM));
        return NULL;
    }
    tls->fd = fd;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_x509_crt_init(&tls->authorities);
    mbedtls_ctr_drbg_init(&tls->random);
    mbedtls_entropy_init(&tls->entropy);

    static const unsigned char personal[] = "ginnOS fetch";
    int ret = mbedtls_ctr_drbg_seed(&tls->random, mbedtls_entropy_func, &tls->entropy, personal,
                                    sizeof(personal) - 1);
    if (ret != 0)
    {
        set_error(tls, ret);
        return open_failed(tls, error, error_size);
    }
    if (verify)
    {
        // a positive result: some certificates in the file were not read
        ret = mbedtls_x509_crt_parse_file(&tls->authorities, TLS_CA_BUNDLE);
        if (ret < 0 || tls->authorities.version == 0)
        {
            snprintf(tls->error, sizeof(tls->error),
                     "cannot read the certificate authorities in %s", TLS_CA_BUNDLE);
            return open_failed(tls, error, error_size);
        }
    }

    ret = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        set_error(tls, ret);
        return open_failed(tls, error, error_size);
    }
    mbedtls_ssl_conf_authmode(&tls->config,
                              verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->authorities, NULL);
    mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->random);

    ret = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (ret == 0)
    {
        ret = mbedtls_ssl_set_hostname(&tls->ssl, host);
    }
    if (ret != 0)
    {
        set_error(tls, ret);
        return open_failed(tls, error, error_size);
    }
    mbedtls_ssl_set_bio(&tls->ssl, tls, bio_send, bio_receive, NULL);

    do
    {
        ret = mbedtls_ssl_handshake(&tls->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
    {
        char why[200];
        uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
        mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
        why[strcspn(why, "\n")] = '\0'; // the first reason
        snprintf(tls->error, sizeof(tls->error), "the certificate is not trusted: %s", why);
        return open_failed(tls, error, error_size);
    }
    if (ret == MBEDTLS_ERR_SSL_CONN_EOF)
    {
        snprintf(tls->error, sizeof(tls->error), "the server closed the connection");
        return open_failed(tls, error, error_size);
    }
    if (ret != 0)
    {
        set_error(tls, ret);
        return open_failed(tls, error, error_size);
    }
    return tls;
}

ssize_t tls_read(tls_t *tls, void *buffer, size_t length)
{
    for (;;)
    {
        int ret = mbedtls_ssl_read(&tls->ssl, buffer, length);
        if (ret > 0)
        {
            return ret;
        }
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == MBEDTLS_ERR_SSL_CONN_EOF)
        {
            return 0;
        }
        // TLS 1.3 servers send session tickets after the handshake
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
            ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        {
            continue;
        }
        set_error(tls, ret);
        return -1;
    }
}

bool tls_write(tls_t *tls, const void *data, size_t length)
{
    const unsigned char *p = data;
    while (length > 0)
    {
        int ret = mbedtls_ssl_write(&tls->ssl, p, length);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (ret < 0)
        {
            set_error(tls, ret);
            return false;
        }
        p += ret;
        length -= (size_t)ret;
    }
    return true;
}

const char *tls_error(const tls_t *tls)
{
    return tls->error;
}

void tls_close(tls_t *tls)
{
    mbedtls_ssl_close_notify(&tls->ssl);
    tls_free(tls);
}
