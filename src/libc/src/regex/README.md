# regex

POSIX regular expressions (`<regex.h>`): `regcomp`, `regexec`, `regfree`
and `regerror`.

`regcomp.c`, `regexec.c`, `tre-mem.c` and `tre.h` are from musl 1.2.5
(https://musl.libc.org), which took them from TRE by Ville Laurikari and
changed them. TRE is under the 2-clause BSD license in each file's head;
musl's changes are under the MIT license in `COPYRIGHT.musl`. They are
kept as musl has them, so a newer musl can be compared with them. The
one change: `tre.h` defines musl's `hidden` mark as empty. The Makefile
builds them without two warnings (signed and unsigned comparisons, and
cases that fall through), which musl's style uses on purpose.

`regerror.c` and `include/regex.h` are ginnOS's own (the same layout of
`regex_t` as musl's).

The matcher works on wide characters: the pattern and the text are UTF-8
(`mbtowc`), and the classes (`[[:alpha:]]`) and `REG_ICASE` use
`<wctype.h>`.
