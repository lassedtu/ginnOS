#!/bin/sh
# fetch the pinned limine binary release into a directory and build the
# limine host tool. the release is a git tag; the script checks the commit,
# so a moved tag does not change the bootloader without a review.
#
# usage: tools/limine/fetch.sh <directory>

set -eu

LIMINE_URL="https://codeberg.org/Limine/Limine.git"
LIMINE_TAG="v11.2.1-binary"
LIMINE_COMMIT="deba356b4a70ee183ec3214864f0ba2ffe510b8c"

dir="$1"

if [ -f "$dir/.ginnos-commit" ] && [ "$(cat "$dir/.ginnos-commit")" = "$LIMINE_COMMIT" ] &&
    [ -x "$dir/limine" ]; then
    exit 0
fi

rm -rf "$dir"
echo "limine: fetching $LIMINE_TAG"
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$LIMINE_TAG" \
    "$LIMINE_URL" "$dir"

commit="$(git -C "$dir" rev-parse HEAD)"
if [ "$commit" != "$LIMINE_COMMIT" ]; then
    echo "limine: $LIMINE_TAG is commit $commit, expected $LIMINE_COMMIT" >&2
    rm -rf "$dir"
    exit 1
fi

# the host tool installs the BIOS boot code into a disk image
make --no-print-directory -C "$dir" CC="${HOST_CC:-cc}" >/dev/null
echo "$LIMINE_COMMIT" > "$dir/.ginnos-commit"
