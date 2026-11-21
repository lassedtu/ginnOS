#!/bin/sh
# fetch the pinned Inter and JetBrains Mono releases and put the TTF files
# bragi uses (with their licences) into a directory. the zips are kept in
# a cache directory, and their SHA-256 is checked, so a changed download
# is not used.
#
# both fonts are under the SIL Open Font License 1.1:
#   Inter:          https://rsms.me/inter/
#   JetBrains Mono: https://www.jetbrains.com/lp/mono/
#
# usage: tools/fonts/fetch.sh <cache directory> <destination directory>
# the fonts go to <destination>/inter and <destination>/jetbrains-mono.

set -eu

INTER_VERSION="4.1"
INTER_URL="https://github.com/rsms/inter/releases/download/v$INTER_VERSION/Inter-$INTER_VERSION.zip"
INTER_SHA256="9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e"
INTER_FILES="extras/ttf/Inter-Regular.ttf extras/ttf/Inter-Medium.ttf
             extras/ttf/Inter-SemiBold.ttf LICENSE.txt"

JBM_VERSION="2.304"
JBM_URL="https://github.com/JetBrains/JetBrainsMono/releases/download/v$JBM_VERSION/JetBrainsMono-$JBM_VERSION.zip"
JBM_SHA256="6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf"
JBM_FILES="fonts/ttf/JetBrainsMono-Regular.ttf fonts/ttf/JetBrainsMono-Bold.ttf
           fonts/ttf/JetBrainsMono-Italic.ttf fonts/ttf/JetBrainsMono-BoldItalic.ttf OFL.txt"

cache="$1"
dest="$2"
mkdir -p "$cache"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

# fetch <name> <url> <sha256> <zip>: download into the cache if needed
fetch() {
    if [ ! -f "$4" ] || [ "$(sha256_of "$4")" != "$3" ]; then
        echo "fonts: fetching $1"
        curl -sfL -o "$4.tmp" "$2"
        if [ "$(sha256_of "$4.tmp")" != "$3" ]; then
            echo "fonts: the $1 download does not have the expected SHA-256" >&2
            rm -f "$4.tmp"
            exit 1
        fi
        mv "$4.tmp" "$4"
    fi
}

# unpack <zip> <directory> <files>: the files, without their folders
unpack() {
    mkdir -p "$2"
    # shellcheck disable=SC2086 # the file list is split on purpose
    unzip -o -j -q "$1" $3 -d "$2"
}

inter_zip="$cache/Inter-$INTER_VERSION.zip"
jbm_zip="$cache/JetBrainsMono-$JBM_VERSION.zip"
fetch "Inter $INTER_VERSION" "$INTER_URL" "$INTER_SHA256" "$inter_zip"
fetch "JetBrains Mono $JBM_VERSION" "$JBM_URL" "$JBM_SHA256" "$jbm_zip"

unpack "$inter_zip" "$dest/inter" "$INTER_FILES"
unpack "$jbm_zip" "$dest/jetbrains-mono" "$JBM_FILES"
