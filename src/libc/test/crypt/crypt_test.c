/**
 * @file crypt_test.c
 * @brief host tests of libc's SHA-256 and crypt(): the FIPS 180 vectors,
 *        the SHA-crypt specification's vectors, and hashes made by
 *        "openssl passwd -5".
 */

#include "crypt.h"
#include "hosttest.h"

#include <stdio.h>
#include <string.h>

static void hex(const uint8_t *d, char *out)
{
    for (int i = 0; i < 32; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
}

static void check_sha(const char *text, const char *want)
{
    uint8_t d[32];
    char got[65];
    sha256(text, strlen(text), d);
    hex(d, got);
    CHECK(strcmp(got, want) == 0);
}

static void check_crypt(const char *key, const char *setting, const char *want)
{
    const char *got = crypt(key, setting);
    CHECK(got && strcmp(got, want) == 0);
    if (got && strcmp(got, want) != 0)
        printf("  crypt(\"%s\", \"%s\")\n    got  %s\n    want %s\n", key, setting, got, want);
}

int main(void)
{
    check_sha("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // the specification's vectors (custom rounds, salts cut at 16)
    check_crypt("Hello world!", "$5$saltstring",
                "$5$saltstring$5B8vYYiY.CVt1RlTTf8KbXBH3hsxY/GNooZaBBGWEc5");
    check_crypt("Hello world!", "$5$rounds=10000$saltstringsaltstring",
                "$5$rounds=10000$saltstringsaltst$3xv.VbSHBb41AL9AvLeujZkZRBAwqFMz2.opqey6IcA");
    check_crypt("This is just a test", "$5$rounds=5000$toolongsaltstring",
                "$5$rounds=5000$toolongsaltstrin$Un/5jzAHMgOGZ5.mWJpuVolil07guHPvOW8mGRcvxa5");
    check_crypt("we have a short salt string but not a short password", "$5$rounds=77777$short",
                "$5$rounds=77777$short$JiO1O3ZpDAxGJeaDIuqCoEFysAe1mZNJRs3pw0KQRd/");
    check_crypt("the minimum number is still observed", "$5$rounds=10$roundstoolow",
                "$5$rounds=1000$roundstoolow$yfvwcWrQ8l/K0DAWyuPMDNHpIVlTQebY9l/gL972bIC");

    // made by "openssl passwd -5 -salt ..." (OpenSSL 3.4)
    check_crypt("password", "$5$saltsalt$", "$5$saltsalt$gOjOtoMpVhru2uyjeJSEc/JaLQWOXMNmlOnj6T4AtC.");
    // openssl passwd cuts a password at 256 characters; crypt() does not
    char long_key[257];
    memset(long_key, 'a', 256);
    long_key[256] = '\0';
    check_crypt(long_key, "$5$longkey", "$5$longkey$SnhS4Fuav79R.//tLQkOhGQb1h/QIEqeCILBukpEJO6");
    check_crypt("p\xc3\xa4sswörd", "$5$\xc3\x9cTF",
                "$5$\xc3\x9cTF$ruixZ4wzsi0uC0sJxzwSk810wfoyG3wBeBmsbmDqyO8");

    // verify: the right one, a wrong one, another scheme
    const char *hash = "$5$saltsalt$gOjOtoMpVhru2uyjeJSEc/JaLQWOXMNmlOnj6T4AtC.";
    CHECK(crypt_verify("password", hash));
    CHECK(!crypt_verify("Password", hash));
    CHECK(!crypt_verify("password", "$6$saltsalt$x"));
    CHECK(!crypt_verify("password", "!"));
    CHECK(crypt("x", "$1$abc") == NULL);

    // a new hash checks, and two new hashes have different salts
    char first[128];
    snprintf(first, sizeof(first), "%s", crypt_new("ginnos", "a", 1));
    CHECK(strncmp(first, "$5$", 3) == 0);
    CHECK(crypt_verify("ginnos", first));
    const char *second = crypt_new("ginnos", "b", 1);
    CHECK(strcmp(first, second) != 0);
    return hosttest_done("libc crypt");
}
