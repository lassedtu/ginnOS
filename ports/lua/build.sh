#!/bin/sh
# build Lua (MIT licence) for ginnOS: the interpreter, /bin/lua. it is
# Lua's own "posix" build (no dynamic loading, no readline).
#
# the pinned release is fetched once, and its SHA-256 is checked.
#
# usage: ports/lua/build.sh <cross prefix> <source cache> <work> <root>
#   <cross prefix>  the ginnOS toolchain, as /path/to/x86_64-ginnos-
#   <source cache>  the release archive is kept here between builds
#   <work>          the folder to build in (made empty first)
#   <root>          the files for the image go here (made empty first)

set -eu

LUA_VERSION="5.4.9"
LUA_URL="https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
LUA_SHA256="2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6"

cross="$1"
cache="$2"
work="$3"
root="$4"
make="${MAKE:-make}"
archive="$cache/lua-$LUA_VERSION.tar.gz"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

mkdir -p "$cache"
if [ ! -f "$archive" ] || [ "$(sha256_of "$archive")" != "$LUA_SHA256" ]; then
    echo "lua: fetching $LUA_URL"
    curl -sfL -o "$archive.tmp" "$LUA_URL"
    if [ "$(sha256_of "$archive.tmp")" != "$LUA_SHA256" ]; then
        echo "lua: the download does not have the expected SHA-256" >&2
        rm -f "$archive.tmp"
        exit 1
    fi
    mv "$archive.tmp" "$archive"
fi

rm -rf "$work" "$root"
mkdir -p "$work" "$root/bin" "$root/usr/share/doc/lua"
tar -xzf "$archive" -C "$work" --strip-components 1
cd "$work/src"

"$make" posix CC="${cross}gcc" AR="${cross}ar rcu" RANLIB="${cross}ranlib" \
    MYCFLAGS="-Os" MYLDFLAGS="-static"
"${cross}strip" -o "$root/bin/lua" lua

# the licence is at the end of lua.h
sed -n '/^\* Copyright (C)/,/^\*\*\*\*/p' lua.h > "$root/usr/share/doc/lua/COPYRIGHT"
[ -s "$root/usr/share/doc/lua/COPYRIGHT" ] || {
    echo "lua: found no licence text in lua.h" >&2
    exit 1
}
echo "lua: /bin/lua ($LUA_VERSION)"
