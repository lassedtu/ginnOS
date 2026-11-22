#!/bin/sh
# build the ginnOS toolchain: binutils and GCC that know the target
# <arch>-ginnos, installed into tools/toolchain/prefix.
#
#   tools/toolchain/build.sh [x86_64|i686]     (x86_64 when not given)
#
# needs: curl, make, a C/C++ compiler, and gmp, mpfr, libmpc, isl (brew).
# it builds the sysroot first (make sysroot), as GCC looks there for the
# libc headers. a full build takes a while; run it once, and again only
# when the patches or versions change. then add tools/toolchain/prefix/bin
# to PATH.

set -e

ARCH=${1:-x86_64}
case $ARCH in
x86_64) TARGET=x86_64-ginnos; BUILD_DIR=build/x86_64 ;;
i686) TARGET=i686-ginnos; BUILD_DIR=build ;;
*) echo "usage: $0 [x86_64|i686]" >&2; exit 1 ;;
esac

BINUTILS=binutils-2.47
GCC=gcc-16.2.0
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PREFIX=$HERE/prefix
SYSROOT=$ROOT/$BUILD_DIR/sysroot
export PATH="$PREFIX/bin:$PATH"

# brew keeps the GCC libraries under their own prefixes
DEPS=""
if command -v brew >/dev/null 2>&1; then
    DEPS="--with-gmp=$(brew --prefix gmp) --with-mpfr=$(brew --prefix mpfr)"
    DEPS="$DEPS --with-mpc=$(brew --prefix libmpc) --with-isl=$(brew --prefix isl)"
fi

# fresh sources with the ginnOS patches
mkdir -p "$HERE/src"
cd "$HERE/src"
fetch() # name, url
{
    if [ ! -f "$1.tar.xz" ]; then
        curl -fSLO "$2/$1.tar.xz"
    fi
    if [ ! -f "$1/.ginnos-patched" ]; then
        rm -rf "$1"
        tar xf "$1.tar.xz"
        (cd "$1" && patch -p1 < "$HERE/patches/$1.patch")
        touch "$1/.ginnos-patched"
    fi
}
fetch $BINUTILS https://ftp.gnu.org/gnu/binutils
fetch $GCC https://ftp.gnu.org/gnu/gcc/$GCC

echo "== the sysroot"
make -C "$ROOT" ARCH=$ARCH sysroot

echo "== binutils for $TARGET"
mkdir -p "$HERE/build/binutils-$ARCH"
cd "$HERE/build/binutils-$ARCH"
"$HERE/src/$BINUTILS/configure" --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" --disable-nls --disable-werror --disable-gdb --disable-gprofng
make -j"$JOBS"
make install

echo "== gcc for $TARGET"
mkdir -p "$HERE/build/gcc-$ARCH"
cd "$HERE/build/gcc-$ARCH"
"$HERE/src/$GCC/configure" --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" --enable-languages=c,c++ --disable-nls --disable-multilib \
    --disable-werror $DEPS
make -j"$JOBS" all-gcc
make -j"$JOBS" all-target-libgcc
make install-gcc install-target-libgcc

echo "== done: $PREFIX/bin/$TARGET-gcc"
