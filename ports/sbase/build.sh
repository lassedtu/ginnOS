#!/bin/sh
# build sbase, the suckless Unix tools (MIT licence), for ginnOS: one
# program, /bin/sbase-box, and a symbolic link to it in /bin for each tool
# (ls, cat, grep, sed, tar, ...). the box picks the tool from the name it
# was started with.
#
# the pinned commit is fetched once with git. sbase is built without
# changes, except that tftp is left out (it needs networking).
#
# usage: ports/sbase/build.sh <cross prefix> <source cache> <work> <root>
#   <cross prefix>  the ginnOS toolchain, as /path/to/x86_64-ginnos-
#   <source cache>  the git clone, kept between builds
#   <work>          the folder to build in (made empty first)
#   <root>          the files for the image go here (made empty first)

set -eu

SBASE_URL="https://git.suckless.org/sbase"
SBASE_COMMIT="c546c3a5724c81cee9a11d816a38ccdf17472129"

cross="$1"
cache="$2"
work="$3"
root="$4"
make="${MAKE:-make}"
yacc="${YACC:-yacc}"

command -v "$yacc" >/dev/null 2>&1 || {
    echo "sbase: bc needs yacc (or set YACC=bison -y)" >&2
    exit 1
}

if ! git -C "$cache" cat-file -e "$SBASE_COMMIT^{commit}" 2>/dev/null; then
    echo "sbase: fetching $SBASE_URL"
    rm -rf "$cache"
    git clone --quiet "$SBASE_URL" "$cache"
    git -C "$cache" cat-file -e "$SBASE_COMMIT^{commit}" 2>/dev/null || {
        echo "sbase: the clone has no commit $SBASE_COMMIT" >&2
        exit 1
    }
fi

rm -rf "$work" "$root"
mkdir -p "$work" "$root/bin" "$root/usr/share/doc/sbase"
git -C "$cache" archive "$SBASE_COMMIT" | tar -x -C "$work"
cd "$work"

# no tftp: remove its source and its line in the list of tools
rm -f tftp.c tftp.1
grep -v '^	tftp\\$' Makefile > Makefile.new
mv Makefile.new Makefile

set -- CC="${cross}gcc" AR="${cross}ar" RANLIB="${cross}ranlib" YACC="$yacc" \
    CFLAGS="-std=c99 -Os" LDFLAGS="-static"

# the two libraries, and the files that are made from other files. the
# host's ranlib can run too (sbase's config.mk), so ranlib them again with
# the cross ranlib.
"$make" "$@" libutil.a libutf.a getconf.h bc.c
"${cross}ranlib" libutil.a libutf.a

# the box: scripts/mkbox gives every tool's main a name of its own and
# writes the table of names into build/sbase-box.c
scripts/mkbox
"${cross}gcc" -std=c99 -Os -static -DPREFIX='"/usr/local"' -D_DEFAULT_SOURCE \
    -D_NETBSD_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64 \
    -I. -o sbase-box build/*.c libutil.a libutf.a
"${cross}strip" -o "$root/bin/sbase-box" sbase-box

# a link for each name in the table
names="$(sed -n 's/^	{"\([^"]*\)", .*/\1/p' build/sbase-box.c)"
[ -n "$names" ] || {
    echo "sbase: found no tool names in build/sbase-box.c" >&2
    exit 1
}
for name in $names; do
    ln -s sbase-box "$root/bin/$name"
done

cp LICENSE "$root/usr/share/doc/sbase/LICENSE"
echo "sbase: $(echo "$names" | wc -w | tr -d ' ') tools in /bin/sbase-box"
