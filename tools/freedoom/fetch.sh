#!/bin/sh
# fetch the pinned Freedoom release and put freedoom1.wad (with its
# license) into a directory. the zip is kept in a cache directory, and its
# SHA-256 is checked, so a changed download is not used.
#
# Freedoom is free game data for Doom engines (BSD license, see
# COPYING.txt): https://freedoom.github.io/
#
# usage: tools/freedoom/fetch.sh <cache directory> <destination directory>

set -eu

VERSION="0.13.0"
URL="https://github.com/freedoom/freedoom/releases/download/v$VERSION/freedoom-$VERSION.zip"
SHA256="3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"

cache="$1"
dest="$2"
zip="$cache/freedoom-$VERSION.zip"

mkdir -p "$cache" "$dest"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

if [ ! -f "$zip" ] || [ "$(sha256_of "$zip")" != "$SHA256" ]; then
    echo "freedoom: fetching $VERSION"
    curl -sfL -o "$zip.tmp" "$URL"
    if [ "$(sha256_of "$zip.tmp")" != "$SHA256" ]; then
        echo "freedoom: the download does not have the expected SHA-256" >&2
        rm -f "$zip.tmp"
        exit 1
    fi
    mv "$zip.tmp" "$zip"
fi

if [ ! -f "$dest/freedoom1.wad" ] || [ "$zip" -nt "$dest/freedoom1.wad" ]; then
    unzip -o -j -q "$zip" "freedoom-$VERSION/freedoom1.wad" "freedoom-$VERSION/COPYING.txt" \
        -d "$dest"
    mv "$dest/COPYING.txt" "$dest/COPYING.freedoom"
fi
