#pragma once

/**
 * @file limits.h
 * @brief sizes of the integer types and system limits.
 */

#define CHAR_BIT  8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN  SCHAR_MIN
#define CHAR_MAX  SCHAR_MAX
#define SHRT_MIN  (-32768)
#define SHRT_MAX  32767
#define USHRT_MAX 65535
#define INT_MIN   (-2147483647 - 1)
#define INT_MAX   2147483647
#define UINT_MAX  4294967295u
#define LONG_MIN  (-__LONG_MAX__ - 1L)
#define LONG_MAX  __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#define LLONG_MIN (-9223372036854775807LL - 1)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615uLL

// longest path, NUL included (the kernel's PATH_MAX)
#define PATH_MAX 256

// largest pipe write that is never split (the kernel's PIPE_BUF)
#define PIPE_BUF 4096

#define MB_LEN_MAX 4 // the longest UTF-8 character

// regular expressions (regex.h)
#define RE_DUP_MAX         255 // the most a {m,n} can repeat
#define CHARCLASS_NAME_MAX 14  // the longest name in [[:name:]]

#define NAME_MAX      255 // the longest name in a folder (ext2)
#define HOST_NAME_MAX 64
#define SSIZE_MAX     __PTRDIFF_MAX__
#define ARG_MAX       131072 // bytes of arguments and environment (the kernel's)
#define LINE_MAX      2048

// the least values POSIX allows (programs use them as fallbacks)
#define _POSIX_ARG_MAX       4096
#define _POSIX_NAME_MAX      14
#define _POSIX_PATH_MAX      256
#define _POSIX_HOST_NAME_MAX 255
#define _POSIX2_LINE_MAX     2048
