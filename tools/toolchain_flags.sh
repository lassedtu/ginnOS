#!/bin/sh
# print extra compiler flags the cross toolchain needs, or nothing.
#
# gcc 16 writes long string data with the .base64 directive when it was
# built for binutils 2.43 or later. a toolchain whose gcc is newer than its
# binutils then fails with "unknown pseudo-op: .base64". in that case this
# script looks for newer binutils of the same target from homebrew and
# prints "-B <dir>", so gcc uses that assembler.
#
# usage: tools/toolchain_flags.sh <cc> <target, e.g. i686-elf>

cc="$1"
target="$2"
tmp="${TMPDIR:-/tmp}/ginnos-toolchain-$$"
mkdir -p "$tmp" || exit 0
trap 'rm -rf "$tmp"' EXIT

# a long string makes gcc 16 use .base64 if it would
awk 'BEGIN { printf "const char probe[] = \""; for (i = 0; i < 300; i++) printf "x\\001"; print "\";" }' \
    > "$tmp/probe.c"

if "$cc" -c -O2 -o "$tmp/probe.o" "$tmp/probe.c" 2>/dev/null; then
    exit 0 # the toolchain works as it is
fi

for prefix in "$(brew --prefix 2>/dev/null)" /opt/homebrew /usr/local; do
    dir="$prefix/opt/$target-binutils/$target/bin"
    if [ -x "$dir/as" ] && "$cc" -B "$dir/" -c -O2 -o "$tmp/probe.o" "$tmp/probe.c" 2>/dev/null; then
        echo "-B $dir/"
        exit 0
    fi
done
