#!/bin/sh
# build curl (the curl licence, MIT-like) for ginnOS: the program
# /bin/curl, and the static library libcurl.a with its headers in the
# sysroot, for programs that are built against it.
#
# https is through the mbedtls port, which must be built first; curl
# trusts the authorities of the ca-certificates port. there is no zlib,
# HTTP/2, IDN, public suffix list or SSH, and no IPv6 (ginnOS has none).
# names are looked up in the program's own thread (no threaded resolver).
# it needs cmake on the build host.
#
# the pinned release is fetched once, and its SHA-256 is checked. curl
# signs its releases with GPG; the hash is of the release that the GitHub
# page and curl.se both give.
#
# usage: ports/curl/build.sh <cross prefix> <source cache> <work> <root> <sysroot>
#   <cross prefix>  the ginnOS toolchain, as /path/to/x86_64-ginnos-
#   <source cache>  the release archive is kept here between builds
#   <work>          the folder to build in (made empty first)
#   <root>          the files for the image go here (made empty first)
#   <sysroot>       the library and the headers go into its usr/

set -eu

CURL_VERSION="8.23.0"
CURL_URL="https://curl.se/download/curl-$CURL_VERSION.tar.xz"
CURL_SHA256="8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5"

here="$(cd "$(dirname "$0")" && pwd)"
toolchain="$(cd "$here/../../tools/toolchain/cmake" && pwd)/ginnos.cmake"
cross="$1"
cache="$2"
work="$3"
root="$4"
sysroot="$5"
make="${MAKE:-make}"
archive="$cache/curl-$CURL_VERSION.tar.xz"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

command -v cmake >/dev/null 2>&1 || {
    echo "curl: cmake is needed to build curl" >&2
    exit 1
}
[ -f "$sysroot/usr/lib/libmbedtls.a" ] || {
    echo "curl: the mbedtls port must be built first" >&2
    exit 1
}

mkdir -p "$cache"
if [ ! -f "$archive" ] || [ "$(sha256_of "$archive")" != "$CURL_SHA256" ]; then
    echo "curl: fetching $CURL_URL"
    curl -sfL -o "$archive.tmp" "$CURL_URL"
    if [ "$(sha256_of "$archive.tmp")" != "$CURL_SHA256" ]; then
        echo "curl: the download does not have the expected SHA-256" >&2
        rm -f "$archive.tmp"
        exit 1
    fi
    mv "$archive.tmp" "$archive"
fi

rm -rf "$work" "$root"
mkdir -p "$work/src" "$work/build" "$root/bin" "$root/usr/share/doc/curl"
tar -xJf "$archive" -C "$work/src" --strip-components 1

cd "$work/build"
cmake "$work/src" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DGINNOS_CROSS="$cross" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_STATIC_CURL=ON \
    -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF \
    -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=none \
    -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF \
    -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_GSSAPI=OFF \
    -DENABLE_IPV6=OFF -DENABLE_THREADED_RESOLVER=OFF -DCURL_DISABLE_LDAP=ON \
    -DPICKY_COMPILER=OFF -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF \
    -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF
"$make"
"$make" install DESTDIR="$work/stage" > /dev/null
stage="$work/stage/usr"

"${cross}strip" -o "$root/bin/curl" "$stage/bin/curl"
cp "$work/src/COPYING" "$root/usr/share/doc/curl/COPYING"

mkdir -p "$sysroot/usr/include" "$sysroot/usr/lib/pkgconfig" "$sysroot/usr/lib/cmake"
rm -rf "$sysroot/usr/include/curl" "$sysroot/usr/lib/cmake/CURL"
cp -R "$stage/include/curl" "$sysroot/usr/include/curl"
cp "$stage/lib/libcurl.a" "$sysroot/usr/lib/libcurl.a"
cp "$stage/lib/pkgconfig/libcurl.pc" "$sysroot/usr/lib/pkgconfig/libcurl.pc"
if [ -d "$stage/lib/cmake/CURL" ]; then
    cp -R "$stage/lib/cmake/CURL" "$sysroot/usr/lib/cmake/CURL"
fi
echo "curl: /bin/curl, and libcurl.a and the headers in the sysroot ($CURL_VERSION)"
