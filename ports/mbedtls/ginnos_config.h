/**
 * @file ginnos_config.h
 * @brief changes to mbedTLS's default configuration for ginnOS (it is
 *        given as MBEDTLS_USER_CONFIG_FILE, read after mbedtls_config.h).
 *
 * MBEDTLS_NET_C: the socket helpers (net_sockets.c) need select(), and
 * programs do their own socket input and output with mbedtls_ssl_set_bio.
 */

#undef MBEDTLS_NET_C
