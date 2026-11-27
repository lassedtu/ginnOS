# Port a program

A port is a program or a library from another project, built with the
ginnOS toolchain and put into the image. Each port is one shell script,
`ports/<name>/build.sh`.

The code: [`ports/`](https://github.com/lassedtu/ginnOS/tree/main/ports),
[`mk/ports.mk`](https://github.com/lassedtu/ginnOS/blob/main/mk/ports.mk)

## The ports

| Port | What it gives | Build system |
| --- | --- | --- |
| `sbase` | the Unix tools (`ls`, `cat`, `grep`, `sed`, `tar` and more) as one program, `/bin/sbase-box`, with a link for each tool | Makefile |
| `lua` | the Lua interpreter, `/bin/lua` | Makefile |
| `sdl3` | SDL 3 as a library in the sysroot, with a video driver for the ginnOS desktop, and `/bin/sdldemo` | CMake, and a patch |
| `mbedtls` | TLS libraries in the sysroot, and `/bin/mbedtls-selftest` | Makefile |
| `ca-certificates` | the certificate authorities, `/etc/ssl/certs/ca-certificates.crt` | none (a file) |
| `curl` | `/bin/curl`, and `libcurl.a` in the sysroot | CMake |

`make` builds the ports in `PORTS`. To build fewer, name them:

```sh
make PORTS="sbase lua"
make PORTS=
```

With `PORTS=`, the image has no ports: no `ls` or `cat`, no `curl`, and
`fetch` has no HTTPS.

The ports need the ginnOS toolchain. See
[The ginnOS toolchain](toolchain.md). `sdl3` and `curl` also need
`cmake` on the build computer.

## How make builds a port

`make` calls the script with five arguments:

| Argument | What it is |
| --- | --- |
| `<cross prefix>` | the toolchain, for example `tools/toolchain/prefix/bin/x86_64-ginnos-`. Add `gcc`, `ar`, `strip` and so on. |
| `<source cache>` | `build/sources/<name>/`. Keep the downloaded source here. `make clean` does not delete it. |
| `<work>` | an empty folder to build in |
| `<root>` | an empty folder for the files of the image, for example `<root>/bin/lua` |
| `<sysroot>` | the sysroot. A library puts its headers and archives into `<sysroot>/usr/`. |

Then `make` copies each `<root>` into the image, after the ginnOS
programs. If a port and ginnOS have the same file, the build stops.

The output of the script goes to `build/<arch>/ports/<name>.log`. If
the script fails, `make` shows the last 20 lines. A port builds again
when one of its files or the sysroot changes.

## Add a port

1. Make `ports/<name>/build.sh`, and make it executable
   (`chmod +x`). Start from
   [`ports/lua/build.sh`](https://github.com/lassedtu/ginnOS/blob/main/ports/lua/build.sh),
   which is the smallest. A port script:

   - pins one release, and checks its SHA-256 before it uses it;
   - downloads it only when the cache does not have it;
   - builds a static program (`-static`);
   - strips the program into `<root>/bin/`;
   - copies the licence to `<root>/usr/share/doc/<name>/`;
   - ends with one line that says what it made. `make` shows that line.

   ```sh
   #!/bin/sh
   set -eu
   cross="$1"; cache="$2"; work="$3"; root="$4"
   make="${MAKE:-make}"

   # fetch the pinned release into $cache and check its SHA-256 (see lua)

   rm -rf "$work" "$root"
   mkdir -p "$work" "$root/bin" "$root/usr/share/doc/hello"
   tar -xzf "$cache/hello-1.0.tar.gz" -C "$work" --strip-components 1
   cd "$work"
   "$make" CC="${cross}gcc" LDFLAGS="-static"
   "${cross}strip" -o "$root/bin/hello" hello
   cp LICENSE "$root/usr/share/doc/hello/"
   echo "hello: /bin/hello (1.0)"
   ```

2. Add the name to `PORTS` in
   [`mk/ports.mk`](https://github.com/lassedtu/ginnOS/blob/main/mk/ports.mk).
   If the port uses the library of another port, add a line such as
   `PORT_NEEDS_curl := mbedtls`, so that `make` builds the other port
   first.
3. Run `make`, then `make run`, and try the program.

### A program with CMake

Give CMake the ginnOS toolchain file,
[`tools/toolchain/cmake/ginnos.cmake`](https://github.com/lassedtu/ginnOS/blob/main/tools/toolchain/cmake/ginnos.cmake).
It sets the compilers, looks for libraries and headers only in the
sysroot, and links statically:

```sh
cmake "$work/src" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DGINNOS_CROSS="$cross" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_SHARED_LIBS=OFF
```

`find_package` and `pkg-config` find the libraries of the other ports
(`SDL3`, `CURL`) in `<sysroot>/usr/lib/cmake` and
`<sysroot>/usr/lib/pkgconfig`. See
[`ports/curl/build.sh`](https://github.com/lassedtu/ginnOS/blob/main/ports/curl/build.sh).

### A library

Install the headers into `<sysroot>/usr/include` and the archive into
`<sysroot>/usr/lib`. Remove the old copy first, so that a file that the
new release does not have goes away. See
[`ports/mbedtls/build.sh`](https://github.com/lassedtu/ginnOS/blob/main/ports/mbedtls/build.sh).

## When the program does not build

| Problem | What to do |
| --- | --- |
| A function or a header is missing | Add it to libc, with a test (see [libc](libc.md#what-it-does-not-have)). |
| The program wants shared libraries or `dlopen` | Turn them off. ginnOS has only static programs. |
| The program wants IPv6, threads for name lookups, or optional libraries (zlib, HTTP/2) | Turn them off with the options of its build system, as the `curl` port does. |
| The program needs code for ginnOS (a video driver, for example) | Put the code in `ports/<name>/`, and a patch that connects it, as the `sdl3` port does. |
