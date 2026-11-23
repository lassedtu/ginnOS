#pragma once

/**
 * @file crypt.h
 * @brief password hashes: crypt() with the SHA-crypt "$5$" scheme (SHA-256),
 *        the one glibc and "openssl passwd -5" make.
 *
 * a hash is "$5$salt$hash" or "$5$rounds=N$salt$hash": the salt is up to
 * 16 characters of [./0-9A-Za-z], the rounds 1000 to 999999999 (5000 when
 * not given).
 */

#include <stddef.h>
#include <stdint.h>

/**
 * hash a password with the scheme, rounds and salt of setting (a whole
 * hash can be given: what is after the salt is not used).
 * @return the hash in a static buffer, or NULL for a setting that is not
 *         "$5$".
 */
char *crypt(const char *key, const char *setting);

/**
 * check a password against a hash, in a time that does not depend on
 * where the two differ (ginnOS).
 * @return 1 when it matches, 0 when it does not or the hash is not "$5$".
 */
int crypt_verify(const char *key, const char *hash);

/**
 * a new hash of a password with a fresh salt and the default rounds
 * (ginnOS). the salt is unique, not secret: the SHA-256 of the time, the
 * CPU's cycle counter, the pid and the seed.
 * @param seed more bytes to mix in (may be NULL).
 * @return the hash in a static buffer.
 */
char *crypt_new(const char *key, const void *seed, size_t seed_length);

/** SHA-256 of data (FIPS 180-4) */
void sha256(const void *data, size_t length, uint8_t out[32]);
