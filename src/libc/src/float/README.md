# float

The exact conversions between numbers and text: `strtod`, `strtof` and
`strtold`, and `printf`'s `%e`, `%f`, `%g` and
`%a`. Both round correctly, so a number printed with enough digits (or
with `%a`) reads back as the same number.

`floatscan.c` is from musl 1.2.5 (https://musl.libc.org, Rich Felker and
others, MIT license in `COPYRIGHT.musl`), kept as musl has it, so a newer
musl can be compared with it. `fmt_fp.c` holds musl's `fmt_u` and `fmt_fp`
from `src/stdio/vfprintf.c`, kept as musl has them below the marked line.

ginnOS's own: `shgetc.h` (floatscan reads from a string, not a FILE),
`floatscan.h`, `strtod.c`, `fmt_fp.h`, and the start of `fmt_fp.c` (it
writes through a function that `stdio.c` gives it).

The Makefile builds these files without three warnings, which musl's
style causes on purpose: signed and unsigned comparisons, operators
without the parentheses that only show their order, and a table of 16
characters filled from a 16-character string.

The work is done in the x87's 80-bit `long double`: `math.c` has the
`long double` helpers these need (`frexpl`, `scalbnl`, `fmodl`,
`copysignl`).
