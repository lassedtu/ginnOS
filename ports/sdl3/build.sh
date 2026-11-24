#!/bin/sh
# build SDL 3 (zlib licence) for ginnOS: the static library libSDL3.a and
# its headers, for programs that are built against the sysroot. a program
# that uses SDL has it linked in; the image gets only a small demo,
# /bin/sdldemo (sdldemo.c).
#
# SDL is built with its generic UNIX code (pthreads, OSS audio on
# /dev/dsp, the unix timer) and our video driver, ginnos/ (windows on the
# desktop through libheimdall). sdl3.patch makes SDL know the ginnOS
# platform and the driver. it needs cmake on the build host.
#
# the pinned release is fetched once, and its SHA-256 is checked.
#
# usage: ports/sdl3/build.sh <cross prefix> <source cache> <work> <root> <sysroot>
#   <cross prefix>  the ginnOS toolchain, as /path/to/x86_64-ginnos-
#   <source cache>  the release archive is kept here between builds
#   <work>          the folder to build in (made empty first)
#   <root>          the files for the image go here (made empty first)
#   <sysroot>       the library and the headers go into its usr/

set -eu

SDL_VERSION="3.4.20"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
SDL_SHA256="f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f"

here="$(cd "$(dirname "$0")" && pwd)"
toolchain="$(cd "$here/../../tools/toolchain/cmake" && pwd)/ginnos.cmake"
cross="$1"
cache="$2"
work="$3"
root="$4"
sysroot="$5"
make="${MAKE:-make}"
archive="$cache/SDL3-$SDL_VERSION.tar.gz"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

command -v cmake >/dev/null 2>&1 || {
    echo "sdl3: cmake is needed to build SDL" >&2
    exit 1
}

mkdir -p "$cache"
if [ ! -f "$archive" ] || [ "$(sha256_of "$archive")" != "$SDL_SHA256" ]; then
    echo "sdl3: fetching $SDL_URL"
    curl -sfL -o "$archive.tmp" "$SDL_URL"
    if [ "$(sha256_of "$archive.tmp")" != "$SDL_SHA256" ]; then
        echo "sdl3: the download does not have the expected SHA-256" >&2
        rm -f "$archive.tmp"
        exit 1
    fi
    mv "$archive.tmp" "$archive"
fi

rm -rf "$work" "$root"
mkdir -p "$work/src" "$work/build" "$root"
tar -xzf "$archive" -C "$work/src" --strip-components 1
patch -s -d "$work/src" -p1 < "$here/sdl3.patch"
cp -R "$here/ginnos" "$work/src/src/video/ginnos"

# only what ginnOS has: no X11 or Wayland, no OpenGL or Vulkan, no
# dynamic loading, no udev or D-Bus, and of the audio back ends only OSS
cd "$work/build"
cmake "$work/src" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DGINNOS_CROSS="$cross" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/install" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OFFSCREEN=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_RENDER_VULKAN=OFF \
    -DSDL_GPU=OFF -DSDL_RENDER_GPU=OFF -DSDL_RPI=OFF -DSDL_ROCKCHIP=OFF \
    -DSDL_VIVANTE=OFF -DSDL_OPENVR=OFF \
    -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_LIBURING=OFF \
    -DSDL_HIDAPI=OFF -DSDL_CAMERA=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF \
    -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF \
    -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF \
    -DSDL_SNDIO=OFF -DSDL_OSS=ON \
    -DSDL_DLOPEN_NOTES=OFF -DSDL_FRIBIDI=OFF -DSDL_LIBTHAI=OFF \
    -DSDL_SYSTEM_ICONV=OFF -DSDL_LIBICONV=OFF
grep -q "define SDL_VIDEO_DRIVER_GINNOS 1" include-config-release/build_config/SDL_build_config.h || {
    echo "sdl3: the ginnOS video driver is not in the build" >&2
    exit 1
}
"$make"
"$make" install > /dev/null

mkdir -p "$sysroot/usr/include" "$sysroot/usr/lib" "$sysroot/usr/share/doc/sdl3"
rm -rf "$sysroot/usr/include/SDL3"
cp -R "$work/install/include/SDL3" "$sysroot/usr/include/SDL3"
cp "$work/install/lib/libSDL3.a" "$sysroot/usr/lib/libSDL3.a"
cp "$work/src/LICENSE.txt" "$sysroot/usr/share/doc/sdl3/LICENSE.txt"

# the demo, /bin/sdldemo, to try the port in the image (with SDL's licence)
mkdir -p "$root/bin" "$root/usr/share/doc/sdl3"
"${cross}gcc" -std=c11 -Os -Wall -Wextra -Werror -static -o "$work/sdldemo" \
    "$here/sdldemo.c" -lSDL3 -lheimdall -lgfx -lm
"${cross}strip" -o "$root/bin/sdldemo" "$work/sdldemo"
cp "$work/src/LICENSE.txt" "$root/usr/share/doc/sdl3/LICENSE.txt"
echo "sdl3: libSDL3.a and the headers in the sysroot ($SDL_VERSION)"
