#!/bin/sh
# build Mbed TLS (Apache-2.0) for ginnOS: the static libraries
# libmbedtls.a, libmbedx509.a and libmbedcrypto.a and their headers, for
# programs that are built against the sysroot (TLS for fetch, and later
# curl). the image gets mbedTLS's own test program, /bin/mbedtls-selftest.
#
# the configuration is mbedTLS's default with the changes in
# ginnos_config.h, which the end of mbedtls_config.h includes: the library
# and the installed headers have the same configuration. randomness comes
# from /dev/urandom (mbedTLS's own Unix code). _POSIX_VERSION is given for
# mbedTLS only: it uses it to find clock_gettime, gmtime_r and socklen_t,
# which our libc has.
#
# the pinned release is fetched once, and its SHA-256 is checked. the
# release has the generated source files, so no Python is needed.
#
# usage: ports/mbedtls/build.sh <cross prefix> <source cache> <work> <root> <sysroot>
#   <cross prefix>  the ginnOS toolchain, as /path/to/x86_64-ginnos-
#   <source cache>  the release archive is kept here between builds
#   <work>          the folder to build in (made empty first)
#   <root>          the files for the image go here (made empty first)
#   <sysroot>       the libraries and the headers go into its usr/

set -eu

MBEDTLS_VERSION="3.6.8"
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
MBEDTLS_SHA256="bc54b818e87f49c0b3281dd85d24adaede0e63b4e822e3fbd49c005a38b9595b"

here="$(cd "$(dirname "$0")" && pwd)"
cross="$1"
cache="$2"
work="$3"
root="$4"
sysroot="$5"
make="${MAKE:-make}"
archive="$cache/mbedtls-$MBEDTLS_VERSION.tar.bz2"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

mkdir -p "$cache"
if [ ! -f "$archive" ] || [ "$(sha256_of "$archive")" != "$MBEDTLS_SHA256" ]; then
    echo "mbedtls: fetching $MBEDTLS_URL"
    curl -sfL -o "$archive.tmp" "$MBEDTLS_URL"
    if [ "$(sha256_of "$archive.tmp")" != "$MBEDTLS_SHA256" ]; then
        echo "mbedtls: the download does not have the expected SHA-256" >&2
        rm -f "$archive.tmp"
        exit 1
    fi
    mv "$archive.tmp" "$archive"
fi

rm -rf "$work" "$root"
mkdir -p "$work" "$root/bin"
tar -xjf "$archive" -C "$work" --strip-components 1
cp "$here/ginnos_config.h" "$work/include/mbedtls/ginnos_config.h"
printf '\n#include "ginnos_config.h"\n' >> "$work/include/mbedtls/mbedtls_config.h"
cd "$work"

cflags="-O2 -D_POSIX_VERSION=200809L"
"$make" -C library static CC="${cross}gcc" AR="${cross}ar" CFLAGS="$cflags"

"${cross}gcc" $cflags -Iinclude -Ilibrary programs/test/selftest.c -o selftest -static \
    -Llibrary -lmbedtls -lmbedx509 -lmbedcrypto
"${cross}strip" -o "$root/bin/mbedtls-selftest" selftest

mkdir -p "$sysroot/usr/include" "$sysroot/usr/lib" "$sysroot/usr/share/doc/mbedtls"
rm -rf "$sysroot/usr/include/mbedtls" "$sysroot/usr/include/psa"
cp -R include/mbedtls "$sysroot/usr/include/mbedtls"
cp -R include/psa "$sysroot/usr/include/psa"
for lib in mbedtls mbedx509 mbedcrypto; do
    cp "library/lib$lib.a" "$sysroot/usr/lib/lib$lib.a"
done
cp LICENSE "$sysroot/usr/share/doc/mbedtls/LICENSE"
echo "mbedtls: the libraries and the headers in the sysroot ($MBEDTLS_VERSION)"
