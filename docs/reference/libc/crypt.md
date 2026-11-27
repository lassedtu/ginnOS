<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;crypt.h&gt;

Source: [`src/libc/include/crypt.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/crypt.h)

password hashes: crypt() with the SHA-crypt "$5$" scheme (SHA-256),
       the one glibc and "openssl passwd -5" make.

a hash is "$5$salt$hash" or "$5$rounds=N$salt$hash": the salt is up to
16 characters of \[./0-9A-Za-z], the rounds 1000 to 999999999 (5000 when
not given).

## crypt

```c
char *crypt(const char *key, const char *setting);
```

hash a password with the scheme, rounds and salt of setting (a whole
hash can be given: what is after the salt is not used).

Returns: the hash in a static buffer, or NULL for a setting that is not "$5$".

## crypt\_verify

```c
int crypt_verify(const char *key, const char *hash);
```

check a password against a hash, in a time that does not depend on
where the two differ (ginnOS).

Returns: 1 when it matches, 0 when it does not or the hash is not "$5$".

## crypt\_new

```c
char *crypt_new(const char *key, const void *seed, size_t seed_length);
```

a new hash of a password with a fresh salt and the default rounds
(ginnOS). the salt is unique, not secret: the SHA-256 of the time, the
CPU's cycle counter, the pid and the seed.

- `seed`: more bytes to mix in (may be NULL).

Returns: the hash in a static buffer.

## sha256

```c
void sha256(const void *data, size_t length, uint8_t out[32]);
```

SHA-256 of data (FIPS 180-4)
