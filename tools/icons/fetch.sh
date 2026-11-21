#!/bin/sh
# fetch the pinned Lucide and Tela releases into a cache directory and
# unpack them into <cache>/src/lucide and <cache>/src/tela. the SHA-256 of
# each download is checked, so a changed download is not used.
#
#   Lucide: https://lucide.dev (ISC; some icons from Feather, MIT)
#   Tela:   https://github.com/vinceliuice/Tela-icon-theme (GPL-3.0)
#
# usage: tools/icons/fetch.sh <cache directory>

set -eu

LUCIDE_VERSION="1.66.0"
LUCIDE_URL="https://github.com/lucide-icons/lucide/releases/download/$LUCIDE_VERSION/lucide-icons-$LUCIDE_VERSION.zip"
LUCIDE_SHA256="b11aa5df304cc8dc910e766271804baabf1aa6d6e6771f81360761e019552e77"
LUCIDE_LICENSE_URL="https://raw.githubusercontent.com/lucide-icons/lucide/$LUCIDE_VERSION/LICENSE"
LUCIDE_LICENSE_SHA256="b495047bd93a9b06913511076f504daba17d5bbeb3e0650f3bb53a4220329c57"

TELA_VERSION="2026-07-07"
TELA_URL="https://github.com/vinceliuice/Tela-icon-theme/archive/refs/tags/$TELA_VERSION.tar.gz"
TELA_SHA256="6b8c28f637067a15551459ab90e6720c5aba1215b777b6f1f506fea188a1ddf9"

cache="$1"
mkdir -p "$cache/dl"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

# fetch <name> <url> <sha256> <file>: download into the cache if needed
fetch() {
    if [ ! -f "$4" ] || [ "$(sha256_of "$4")" != "$3" ]; then
        echo "icons: fetching $1"
        curl -sfL -o "$4.tmp" "$2"
        if [ "$(sha256_of "$4.tmp")" != "$3" ]; then
            echo "icons: the $1 download does not have the expected SHA-256" >&2
            rm -f "$4.tmp"
            exit 1
        fi
        mv "$4.tmp" "$4"
    fi
}

fetch "Lucide $LUCIDE_VERSION" "$LUCIDE_URL" "$LUCIDE_SHA256" "$cache/dl/lucide-$LUCIDE_VERSION.zip"
fetch "the Lucide licence" "$LUCIDE_LICENSE_URL" "$LUCIDE_LICENSE_SHA256" \
    "$cache/dl/lucide-$LUCIDE_VERSION-LICENSE"
fetch "Tela $TELA_VERSION" "$TELA_URL" "$TELA_SHA256" "$cache/dl/tela-$TELA_VERSION.tar.gz"

# unpack once per version (a stamp file names the version unpacked)
if [ "$(cat "$cache/src/lucide/.version" 2>/dev/null)" != "$LUCIDE_VERSION" ]; then
    rm -rf "$cache/src/lucide"
    mkdir -p "$cache/src/lucide"
    unzip -q "$cache/dl/lucide-$LUCIDE_VERSION.zip" 'icons/*.svg' -d "$cache/src/lucide"
    cp "$cache/dl/lucide-$LUCIDE_VERSION-LICENSE" "$cache/src/lucide/LICENSE"
    echo "$LUCIDE_VERSION" > "$cache/src/lucide/.version"
fi
if [ "$(cat "$cache/src/tela/.version" 2>/dev/null)" != "$TELA_VERSION" ]; then
    rm -rf "$cache/src/tela"
    mkdir -p "$cache/src/tela"
    tar xzf "$cache/dl/tela-$TELA_VERSION.tar.gz" -C "$cache/src/tela" --strip-components 1 \
        "Tela-icon-theme-$TELA_VERSION/src/scalable" "Tela-icon-theme-$TELA_VERSION/links/scalable" \
        "Tela-icon-theme-$TELA_VERSION/COPYING"
    echo "$TELA_VERSION" > "$cache/src/tela/.version"
fi
